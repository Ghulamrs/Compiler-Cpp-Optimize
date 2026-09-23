#include "Optimizer.h"

#include <cstring>
#include <algorithm>
#include <map>

IrOp IrOp::from(const Op &o) {
    IrOp r;
    r.kind = o.kind;
    r.text.assign(o.text.p, o.text.n);
    r.disp = o.disp;
    r.hasDisp = o.hasDisp;
    r.uimm = o.uimm;
    r.immNeg = o.immNeg;
    r.immNumeric = o.immNumeric;
    return r;
}

Op IrOp::view() const {
    return { kind, Str(text), disp, hasDisp, uimm, immNeg, immNumeric };
}

bool IrOp::same(const IrOp &o) const {
    return kind == o.kind && text == o.text && disp == o.disp &&
           hasDisp == o.hasDisp && uimm == o.uimm && immNeg == o.immNeg &&
           immNumeric == o.immNumeric;
}

// ---------------------------------------------------------------------------
// The register model.

// **Every table in this file is a constant with a constant initialiser.** Not
// a static local built on first use, which would be a guard - a lock, in
// effect - taken by every thread of the driver's pool on every call.

namespace {

const unsigned kAll = 0xFFFFFFFFu;
enum { kRax = 0, kRbx = 1, kRcx = 2, kRdx = 3, kRsi = 4, kRdi = 5, kRbp = 6, kRsp = 7,
       kGprCount = 16, kXmm0 = 16 };

constexpr unsigned bit(int r) { return 1u << static_cast<unsigned>(r); }

// The four names of each general register, widest first.
constexpr const char *const kGprNames[kGprCount][4] = {
    { "%rax", "%eax",  "%ax",   "%al"   },
    { "%rbx", "%ebx",  "%bx",   "%bl"   },
    { "%rcx", "%ecx",  "%cx",   "%cl"   },
    { "%rdx", "%edx",  "%dx",   "%dl"   },
    { "%rsi", "%esi",  "%si",   "%sil"  },
    { "%rdi", "%edi",  "%di",   "%dil"  },
    { "%rbp", "%ebp",  "%bp",   "%bpl"  },
    { "%rsp", "%esp",  "%sp",   "%spl"  },
    { "%r8",  "%r8d",  "%r8w",  "%r8b"  },
    { "%r9",  "%r9d",  "%r9w",  "%r9b"  },
    { "%r10", "%r10d", "%r10w", "%r10b" },
    { "%r11", "%r11d", "%r11w", "%r11b" },
    { "%r12", "%r12d", "%r12w", "%r12b" },
    { "%r13", "%r13d", "%r13w", "%r13b" },
    { "%r14", "%r14d", "%r14w", "%r14b" },
    { "%r15", "%r15d", "%r15w", "%r15b" },
};

// **Which 64-bit register a name is, and how wide the name is.** The high
// bytes %ah..%dh answer with width 0: they alias their register, so they count
// as a read and a part write of it, but nothing here will rename one.

// An x87 name answers -2 and is ignored; anything else answers -1, and the
// instruction naming it is treated as doing anything at all.
int regLookup(const std::string &name, int &width) {
    width = 0;
    if (name.size() < 3 || name[0] != '%') return -1;
    if (name.compare(0, 4, "%xmm") == 0) {
        int n = 0;
        for (std::size_t i = 4; i < name.size(); i++) {
            if (name[i] < '0' || name[i] > '9') return -1;
            n = n * 10 + (name[i] - '0');
            if (n > 15) return -1;
        }
        if (name.size() == 4) return -1;
        width = 16;
        return kXmm0 + n;
    }
    if (name[1] == 's' && name[2] == 't') return -2;
    const std::size_t n = name.size();
    // r8..r15, with d, w or b for the narrower names.
    if (name[1] == 'r' && name[2] >= '0' && name[2] <= '9') {
        int v = name[2] - '0';
        std::size_t i = 3;
        if (i < n && name[i] >= '0' && name[i] <= '9') v = v * 10 + (name[i++] - '0');
        if (v < 8 || v > 15) return -1;
        if (i == n) { width = 8; return v; }
        if (i + 1 != n) return -1;
        switch (name[i]) {
        case 'd': width = 4; return v;
        case 'w': width = 2; return v;
        case 'b': width = 1; return v;
        default: return -1;
        }
    }
    // The eight legacy registers: a two-letter core, with r or e in front for
    // the wide names, and l behind for the low byte of si, di, bp and sp.
    std::size_t at = 1;
    int w = 2;
    if (name[1] == 'r') { at = 2; w = 8; }
    else if (name[1] == 'e') { at = 2; w = 4; }
    if (at + 2 != n && at + 3 != n) return -1;
    const char c0 = name[at], c1 = name[at + 1];
    int r = -1;
    bool eightBit = false;
    if (c1 == 'x' || c1 == 'l' || c1 == 'h') {
        switch (c0) {
        case 'a': r = kRax; break;
        case 'b': r = kRbx; break;
        case 'c': r = kRcx; break;
        case 'd': r = kRdx; break;
        default: return -1;
        }
        if (c1 != 'x') {
            if (w != 2) return -1;
            eightBit = true;
            w = c1 == 'l' ? 1 : 0;
        }
    } else if (c0 == 's' && c1 == 'i') r = kRsi;
    else if (c0 == 'd' && c1 == 'i') r = kRdi;
    else if (c0 == 'b' && c1 == 'p') r = kRbp;
    else if (c0 == 's' && c1 == 'p') r = kRsp;
    else return -1;
    if (at + 3 == n) {
        // sil, dil, bpl, spl - and nothing else has a third letter.
        if (eightBit || w != 2 || name[at + 2] != 'l' || r < kRsi) return -1;
        w = 1;
    }
    width = w;
    return r;
}

const char *regName(int canon, int width) {
    int w = width == 8 ? 0 : width == 4 ? 1 : width == 2 ? 2 : 3;
    return kGprNames[canon][w];
}

// Whether naming this register at this width costs a REX prefix.
bool needsRex(int canon, int width) {
    if (canon >= 8) return true;
    return width == 1 && canon >= kRsi && canon <= kRsp;
}

// ---------------------------------------------------------------------------
// The instruction model.

enum { kFlagsDef = 1, kFlagsUse = 2 };

struct Mnemonic {
    const char *name;
    unsigned char len;
    IrSem::Class cls;
    unsigned char flags;
};
// The length is part of the entry, so a lookup is a length test and a memcmp.
#define MN(name, cls, flags) { name, sizeof(name) - 1, cls, flags }

// **Every mnemonic the x86-64 walker writes, and what it does.** A mnemonic
// that is not here is Unknown, which reads and writes everything - so a new
// instruction in the walker costs an optimisation, never a miscompile.
constexpr Mnemonic kMnemonics[] = {
    MN("mov", IrSem::Move, 0),       MN("movq", IrSem::Move, 0),
    MN("movl", IrSem::Move, 0),      MN("movw", IrSem::Move, 0),
    MN("movb", IrSem::Move, 0),      MN("movabs", IrSem::Move, 0),
    MN("movzbq", IrSem::Move, 0),    MN("movzbl", IrSem::Move, 0),
    MN("movzwq", IrSem::Move, 0),    MN("movzwl", IrSem::Move, 0),
    MN("movzbw", IrSem::Move, 0),
    MN("movsbq", IrSem::Move, 0),    MN("movswq", IrSem::Move, 0),
    MN("movslq", IrSem::Move, 0),    MN("movsbl", IrSem::Move, 0),
    MN("movswl", IrSem::Move, 0),    MN("movsbw", IrSem::Move, 0),
    MN("lea", IrSem::Move, 0),
    MN("movsd", IrSem::Move, 0),     MN("movss", IrSem::Move, 0),
    MN("movd", IrSem::Move, 0),      MN("movaps", IrSem::Move, 0),
    MN("movapd", IrSem::Move, 0),    MN("movdqa", IrSem::Move, 0),
    MN("movups", IrSem::Move, 0),    MN("movupd", IrSem::Move, 0),
    MN("cvtsi2sdq", IrSem::Move, 0), MN("cvtsi2sdl", IrSem::Move, 0),
    MN("cvtsi2sd", IrSem::Move, 0),  MN("cvtsi2ssq", IrSem::Move, 0),
    MN("cvtsi2ssl", IrSem::Move, 0), MN("cvtsi2ss", IrSem::Move, 0),
    MN("cvttsd2si", IrSem::Move, 0), MN("cvttsd2siq", IrSem::Move, 0),
    MN("cvttsd2sil", IrSem::Move, 0),MN("cvttss2si", IrSem::Move, 0),
    MN("cvttss2siq", IrSem::Move, 0),MN("cvttss2sil", IrSem::Move, 0),
    MN("cvtss2sd", IrSem::Move, 0),  MN("cvtsd2ss", IrSem::Move, 0),
    MN("sqrtsd", IrSem::Move, 0),    MN("sqrtss", IrSem::Move, 0),

    MN("add", IrSem::Rmw, kFlagsDef),  MN("sub", IrSem::Rmw, kFlagsDef),
    MN("and", IrSem::Rmw, kFlagsDef),  MN("or", IrSem::Rmw, kFlagsDef),
    MN("xor", IrSem::Rmw, kFlagsDef),  MN("imul", IrSem::Rmw, kFlagsDef),
    MN("addq", IrSem::Rmw, kFlagsDef), MN("subq", IrSem::Rmw, kFlagsDef),
    MN("andq", IrSem::Rmw, kFlagsDef), MN("orq", IrSem::Rmw, kFlagsDef),
    MN("xorq", IrSem::Rmw, kFlagsDef), MN("imulq", IrSem::Rmw, kFlagsDef),
    MN("addl", IrSem::Rmw, kFlagsDef), MN("subl", IrSem::Rmw, kFlagsDef),
    MN("andl", IrSem::Rmw, kFlagsDef), MN("orl", IrSem::Rmw, kFlagsDef),
    MN("xorl", IrSem::Rmw, kFlagsDef), MN("imull", IrSem::Rmw, kFlagsDef),
    MN("addw", IrSem::Rmw, kFlagsDef), MN("subw", IrSem::Rmw, kFlagsDef),
    MN("andw", IrSem::Rmw, kFlagsDef), MN("orw", IrSem::Rmw, kFlagsDef),
    MN("xorw", IrSem::Rmw, kFlagsDef),
    MN("addb", IrSem::Rmw, kFlagsDef), MN("subb", IrSem::Rmw, kFlagsDef),
    MN("andb", IrSem::Rmw, kFlagsDef), MN("orb", IrSem::Rmw, kFlagsDef),
    MN("xorb", IrSem::Rmw, kFlagsDef),
    // A shift by zero leaves the flags alone, so a shift is neither a
    // writer to stop at nor a reader; the flags stay live through it.
    MN("shl", IrSem::Rmw, 0),  MN("sal", IrSem::Rmw, 0),
    MN("sar", IrSem::Rmw, 0),  MN("shr", IrSem::Rmw, 0),
    MN("shlq", IrSem::Rmw, 0), MN("salq", IrSem::Rmw, 0),
    MN("sarq", IrSem::Rmw, 0), MN("shrq", IrSem::Rmw, 0),
    MN("shll", IrSem::Rmw, 0), MN("sall", IrSem::Rmw, 0),
    MN("sarl", IrSem::Rmw, 0), MN("shrl", IrSem::Rmw, 0),
    MN("shlw", IrSem::Rmw, 0), MN("sarw", IrSem::Rmw, 0),
    MN("shrw", IrSem::Rmw, 0), MN("shlb", IrSem::Rmw, 0),
    MN("sarb", IrSem::Rmw, 0), MN("shrb", IrSem::Rmw, 0),
    MN("addsd", IrSem::Rmw, 0), MN("subsd", IrSem::Rmw, 0),
    MN("mulsd", IrSem::Rmw, 0), MN("divsd", IrSem::Rmw, 0),
    MN("addss", IrSem::Rmw, 0), MN("subss", IrSem::Rmw, 0),
    MN("mulss", IrSem::Rmw, 0), MN("divss", IrSem::Rmw, 0),
    MN("minsd", IrSem::Rmw, 0), MN("maxsd", IrSem::Rmw, 0),
    MN("pxor", IrSem::Rmw, 0),  MN("xorps", IrSem::Rmw, 0),
    MN("xorpd", IrSem::Rmw, 0), MN("andps", IrSem::Rmw, 0),
    MN("andpd", IrSem::Rmw, 0), MN("orps", IrSem::Rmw, 0),
    MN("orpd", IrSem::Rmw, 0),

    MN("cmp", IrSem::Cmp, kFlagsDef),   MN("cmpq", IrSem::Cmp, kFlagsDef),
    MN("cmpl", IrSem::Cmp, kFlagsDef),  MN("cmpw", IrSem::Cmp, kFlagsDef),
    MN("cmpb", IrSem::Cmp, kFlagsDef),  MN("test", IrSem::Cmp, kFlagsDef),
    MN("testq", IrSem::Cmp, kFlagsDef), MN("testl", IrSem::Cmp, kFlagsDef),
    MN("testw", IrSem::Cmp, kFlagsDef), MN("testb", IrSem::Cmp, kFlagsDef),
    MN("ucomisd", IrSem::Cmp, kFlagsDef), MN("ucomiss", IrSem::Cmp, kFlagsDef),
    MN("comisd", IrSem::Cmp, kFlagsDef),  MN("comiss", IrSem::Cmp, kFlagsDef),

    MN("neg", IrSem::Unary, kFlagsDef),  MN("negq", IrSem::Unary, kFlagsDef),
    MN("negl", IrSem::Unary, kFlagsDef), MN("not", IrSem::Unary, 0),
    MN("notq", IrSem::Unary, 0),         MN("notl", IrSem::Unary, 0),
    // inc and dec leave CF, so they are not a writer of all the flags.
    MN("inc", IrSem::Unary, 0),   MN("dec", IrSem::Unary, 0),
    MN("incq", IrSem::Unary, 0),  MN("decq", IrSem::Unary, 0),
    MN("incl", IrSem::Unary, 0),  MN("decl", IrSem::Unary, 0),

    MN("push", IrSem::Push, 0), MN("pushq", IrSem::Push, 0),
    MN("pop", IrSem::Pop, 0),   MN("popq", IrSem::Pop, 0),

    MN("cqo", IrSem::Cqo, 0),   MN("cqto", IrSem::Cqo, 0),
    MN("cdq", IrSem::Cdq, 0),   MN("cltd", IrSem::Cdq, 0),
    MN("cltq", IrSem::Cltq, 0), MN("cdqe", IrSem::Cltq, 0),
    MN("rep movsq", IrSem::Rep, 0),

    // The flags after a division are undefined, so no reader may depend on
    // what was there before it: it counts as a writer.
    MN("idiv", IrSem::Div, kFlagsDef),  MN("div", IrSem::Div, kFlagsDef),
    MN("idivq", IrSem::Div, kFlagsDef), MN("divq", IrSem::Div, kFlagsDef),
    MN("idivl", IrSem::Div, kFlagsDef), MN("divl", IrSem::Div, kFlagsDef),
    MN("mul", IrSem::Div, kFlagsDef),   MN("mulq", IrSem::Div, kFlagsDef),
    MN("mull", IrSem::Div, kFlagsDef),

    MN("fldt", IrSem::X87, 0),   MN("fldl", IrSem::X87, 0),
    MN("flds", IrSem::X87, 0),   MN("fld", IrSem::X87, 0),
    MN("fld1", IrSem::X87, 0),   MN("fldz", IrSem::X87, 0),
    MN("fstpt", IrSem::X87, 0),  MN("fstpl", IrSem::X87, 0),
    MN("fstps", IrSem::X87, 0),  MN("fstp", IrSem::X87, 0),
    MN("fildq", IrSem::X87, 0),  MN("fildl", IrSem::X87, 0),
    MN("fild", IrSem::X87, 0),   MN("fistpq", IrSem::X87, 0),
    MN("fistpl", IrSem::X87, 0), MN("fisttpq", IrSem::X87, 0),
    MN("fisttpl", IrSem::X87, 0),MN("fldcw", IrSem::X87, 0),
    MN("fnstcw", IrSem::X87, 0), MN("fstcw", IrSem::X87, 0),
    MN("faddp", IrSem::X87, 0),  MN("fsubp", IrSem::X87, 0),
    MN("fsubrp", IrSem::X87, 0), MN("fmulp", IrSem::X87, 0),
    MN("fdivp", IrSem::X87, 0),  MN("fdivrp", IrSem::X87, 0),
    MN("fxch", IrSem::X87, 0),   MN("fchs", IrSem::X87, 0),
    MN("fabs", IrSem::X87, 0),
    MN("fucomip", IrSem::X87, kFlagsDef), MN("fcomip", IrSem::X87, kFlagsDef),
    MN("fucomi", IrSem::X87, kFlagsDef),  MN("fcomi", IrSem::X87, kFlagsDef),

    MN("call", IrSem::Call, 0),
    MN("ret", IrSem::Ret, 0),
    MN("leave", IrSem::Leave, 0),
    MN("nop", IrSem::Nop, 0),
};

const Mnemonic *findMnemonic(const std::string &m) {
    const std::size_t n = m.size();
    for (const Mnemonic &e : kMnemonics)
        if (n == e.len && std::memcmp(m.data(), e.name, n) == 0) return &e;
    return nullptr;
}

bool startsWith(const std::string &s, const char *p) {
    return s.compare(0, std::strlen(p), p) == 0;
}

bool isShift(const std::string &m) {
    return startsWith(m, "sh") || startsWith(m, "sa");
}

bool isMultiply(const std::string &m) {
    return m == "imul" || m == "imulq" || m == "imull" || m == "mul" ||
           m == "mulq" || m == "mull";
}

bool isX87Store(const std::string &m) {
    return startsWith(m, "fst") || startsWith(m, "fist") || startsWith(m, "fnst");
}

// What must be intact when a function returns: the return registers and the
// ones the callee preserves.
constexpr unsigned kRetLive = bit(kRax) | bit(kRdx) | bit(kRbx) | bit(kRbp) | bit(kRsp) |
                          bit(12) | bit(13) | bit(14) | bit(15) |
                          bit(kXmm0) | bit(kXmm0 + 1);

struct Describer {
    IrSem s;
    bool bad = false;

    void read(const IrOp &o, bool fixed) {
        if (o.kind != Op::Reg && o.kind != Op::Mem && o.kind != Op::Ind) return;
        int w;
        int r = regLookup(o.text, w);
        if (r == -2) return;
        if (r < 0) { bad = true; return; }
        s.use |= bit(r);
        if (fixed) s.fixed |= bit(r);
    }

    // `alsoRead`: the class reads the destination before writing it.
    void write(const IrOp &o, bool alsoRead, bool isDst) {
        if (o.kind == Op::Mem) { s.memWrite = true; read(o, false); return; }
        if (o.kind == Op::Rip) { s.memWrite = true; return; }
        if (o.kind != Op::Reg) { bad = true; return; }
        int w;
        int r = regLookup(o.text, w);
        if (r == -2) return;
        if (r < 0) { bad = true; return; }
        if (alsoRead) { s.use |= bit(r); s.fixed |= bit(r); }
        if (w == 8 || w == 4) {
            s.def |= bit(r);
            if (isDst && !alsoRead) { s.dstReg = r; s.dstWidth = w; }
        } else {
            // A part write keeps the rest of the register, so it reads it.
            s.part |= bit(r);
            s.use |= bit(r);
            s.fixed |= bit(r);
        }
    }

    void everything() {
        s = IrSem();
        s.cls = IrSem::Unknown;
        s.use = kAll;
        s.fixed = kAll;
        s.flagsUse = s.flagsDef = true;
        s.memWrite = true;
    }
};

IrSem describe(const IrIns &i) {
    Describer d;
    IrSem &s = d.s;
    const Mnemonic *e = findMnemonic(i.m);
    if (e != nullptr) {
        s.cls = e->cls;
        s.flagsDef = (e->flags & kFlagsDef) != 0;
        s.flagsUse = (e->flags & kFlagsUse) != 0;
    } else if (!i.m.empty() && i.m[0] == 'j') {
        s.cls = IrSem::Jump;
        s.flagsUse = i.m != "jmp";
    } else if (startsWith(i.m, "set")) {
        s.cls = IrSem::Set;
        s.flagsUse = true;
    } else if (startsWith(i.m, "cmov")) {
        s.cls = IrSem::Rmw;
        s.flagsUse = true;
    } else {
        d.everything();
        return s;
    }
    if (s.cls == IrSem::Rmw && i.operands == 1) {
        // imul %r is rax:rdx = rax * r; shl %r is a shift by one.
        s.cls = isMultiply(i.m) ? IrSem::Div : IrSem::Unary;
    }

    switch (s.cls) {
    case IrSem::Move:
        if (i.operands != 2) { d.bad = true; break; }
        d.read(i.a, false);
        d.write(i.b, false, true);
        break;
    case IrSem::Rmw:
        if (i.operands != 2) { d.bad = true; break; }
        d.read(i.a, isShift(i.m) && i.a.kind == Op::Reg);
        d.write(i.b, true, false);
        if (s.flagsUse) {
            // cmov: the destination keeps its value when the condition fails.
            s.part |= s.def;
            s.def = 0;
        }
        break;
    case IrSem::Cmp:
        if (i.operands != 2) { d.bad = true; break; }
        d.read(i.a, false);
        d.read(i.b, false);
        break;
    case IrSem::Set:
        if (i.operands != 1) { d.bad = true; break; }
        d.write(i.a, true, false);
        break;
    case IrSem::Unary:
        if (i.operands != 1) { d.bad = true; break; }
        d.write(i.a, true, false);
        break;
    case IrSem::Push:
        if (i.operands != 1) { d.bad = true; break; }
        d.read(i.a, false);
        s.use |= bit(kRsp); s.part |= bit(kRsp); s.fixed |= bit(kRsp);
        s.memWrite = true;
        break;
    case IrSem::Pop:
        if (i.operands != 1) { d.bad = true; break; }
        d.write(i.a, false, true);
        s.use |= bit(kRsp); s.part |= bit(kRsp); s.fixed |= bit(kRsp);
        break;
    case IrSem::Cqo:
    case IrSem::Cdq:
        if (i.operands != 0) { d.bad = true; break; }
        s.use |= bit(kRax); s.fixed |= bit(kRax);
        s.def |= bit(kRdx);
        break;
    case IrSem::Cltq:
        if (i.operands != 0) { d.bad = true; break; }
        s.use |= bit(kRax); s.fixed |= bit(kRax);
        s.def |= bit(kRax);
        break;
    case IrSem::Div:
        if (i.operands != 1) { d.bad = true; break; }
        d.read(i.a, false);
        s.use |= bit(kRax) | bit(kRdx); s.fixed |= bit(kRax) | bit(kRdx);
        s.def |= bit(kRax) | bit(kRdx);
        break;
    case IrSem::X87:
        if (i.operands > 1) { d.bad = true; break; }
        if (i.operands == 1) {
            d.read(i.a, false);
            if (isX87Store(i.m)) s.memWrite = true;
        }
        break;
    case IrSem::Call:
        // Everything, here: this has no ABI to hand. Optimizer::semantics()
        // narrows it to what the convention says a call reads and writes.
        if (i.operands == 1) d.read(i.a, false);
        s.callee = s.use;
        s.use = kAll; s.fixed = kAll;
        s.memWrite = true;
        break;
    case IrSem::Jump:
        // Only what it jumps through: what is live after it is the successors'
        // to say, through the graph. `use = kAll` here, from the per-run days,
        // made every chunk that ends in a jump read everything.
        if (i.operands == 1) d.read(i.a, false);
        s.fixed |= s.use;
        break;
    case IrSem::Ret:
        s.use = kRetLive; s.fixed = kRetLive;
        break;
    case IrSem::Leave:
        s.use |= bit(kRbp) | bit(kRsp); s.fixed |= bit(kRbp) | bit(kRsp);
        s.part |= bit(kRbp) | bit(kRsp);
        break;
    case IrSem::Nop:
        break;
    case IrSem::Rep: {
        if (i.operands != 0) { d.bad = true; break; }
        const unsigned r = bit(kRsi) | bit(kRdi) | bit(kRcx);
        s.use |= r; s.part |= r; s.fixed |= r;
        s.memWrite = true;
        break;
    }
    case IrSem::Unknown:
        d.bad = true;
        break;
    }
    if (d.bad) d.everything();
    return s;
}

// Whether `o` names canonical register `r`, and at what width. A memory
// base or an indirect target is always the 64-bit name.
bool namesReg(const IrOp &o, int r, int &width) {
    if (o.kind != Op::Reg && o.kind != Op::Mem && o.kind != Op::Ind) return false;
    int w;
    if (regLookup(o.text, w) != r) return false;
    width = w;
    return true;
}

// **Whether operand k of x is a read position** - a place a rename of the
// register it names is a rename of a read. A memory base or an indirect
// target is one always; a register is one where its class reads that side.
bool readsAt(const IrIns &x, const IrSem &s, int k) {
    const IrOp &o = k == 0 ? x.a : x.b;
    if (o.kind == Op::Mem || o.kind == Op::Ind) return true;
    if (o.kind != Op::Reg) return false;
    switch (s.cls) {
    case IrSem::Move: case IrSem::Rmw: case IrSem::Push: case IrSem::Div:
    case IrSem::Call: case IrSem::Jump:
        return k == 0;
    case IrSem::Cmp:
        return true;
    default:
        return false;
    }
}

bool isGpr64(const IrOp &o, int &canon) {
    int w;
    if (o.kind != Op::Reg) return false;
    canon = regLookup(o.text, w);
    return canon >= 0 && canon < kGprCount && w == 8;
}

// A general register a pass may allocate: any but the two that address the frame.
bool allocatable(int r) {
    return r >= 0 && r < kGprCount && r != kRbp && r != kRsp;
}

bool isReg(const IrOp &o, const char *name) {
    return o.kind == Op::Reg && o.text == name;
}

bool isZeroImm(const IrOp &o) {
    return o.kind == Op::Imm && o.immNumeric && o.uimm == 0;
}

IrOp regOp(const char *name) {
    IrOp o;
    o.kind = Op::Reg;
    o.text = name;
    return o;
}

void refresh(IrIns &x) {
    x.sem = describe(x);
    x.semValid = true;
}

// How far forward a pass looks from one instruction: a long run costs its length.
const std::size_t kScanLimit = 256;

// A run ends at anything that can leave it other than by falling through.
bool endsRun(const std::string &m) {
    return (!m.empty() && m[0] == 'j') || m == "call" || m == "ret";
}

// **The general registers whose 64-bit self-move is a true no-op.** Moving
// a 32-bit register onto itself zero-extends into the whole register, and a
// 16- or 8-bit one is a partial write; only these names may be dropped.
bool is64Gpr(const std::string &r) {
    int w;
    int c = regLookup(r, w);
    return c >= 0 && c < kGprCount && w == 8;
}

// **The 64-bit move is spelled `mov` by the walker and `movq` in a few
// places**; with a 64-bit general register on one side they are the same
// instruction, and only then is the width certain.
bool isMov64(const IrIns &i) {
    return (i.m == "mov" || i.m == "movq") && i.operands == 2;
}

bool isRegToMem(const IrIns &i) {
    return isMov64(i) && i.a.kind == Op::Reg && i.b.kind == Op::Mem &&
           is64Gpr(i.a.text);
}

bool isMemToReg(const IrIns &i) {
    return isMov64(i) && i.a.kind == Op::Mem && i.b.kind == Op::Reg &&
           is64Gpr(i.b.text);
}

// A push or pop of a 64-bit general register other than the two that address
// the stack, which is every one the walker's expression stack uses.
bool isStackOp(const IrIns &i, const char *m) {
    return i.m == m && i.operands == 1 && i.a.kind == Op::Reg &&
           is64Gpr(i.a.text) && i.a.text != "%rsp" && i.a.text != "%rbp";
}

} // namespace

// ---------------------------------------------------------------------------
// Collecting a function.

IrChunk &Optimizer::chunk() {
    if (chunks_.empty() || !open_) {
        chunks_.push_back(IrChunk());
        open_ = true;
    }
    return chunks_.back();
}

void Optimizer::add(IrIns &&i) {
    // Nothing reaches here: the last chunk ended in a jump or a return and no
    // label has been defined since.
    if (unreachable_ && level_ >= 1) {
        removed_++;
        return;
    }
    bool ends = endsRun(i.m);
    bool leaves = i.m == "jmp" || i.m == "ret";
    chunk().ins.push_back(std::move(i));
    if (ends) {
        open_ = false;
        unreachable_ = leaves;
    }
}

void Optimizer::ins(const std::string &m) {
    lastWasCall_ = m == "call";
    IrIns i; i.m = m; i.operands = 0;
    add(std::move(i));
}

void Optimizer::ins(const std::string &m, const Op &a) {
    lastWasCall_ = m == "call";
    IrIns i; i.m = m; i.operands = 1; i.a = IrOp::from(a);
    add(std::move(i));
}

void Optimizer::ins(const std::string &m, const Op &a, const Op &b) {
    lastWasCall_ = m == "call";
    IrIns i; i.m = m; i.operands = 2; i.a = IrOp::from(a); i.b = IrOp::from(b);
    add(std::move(i));
}

void Optimizer::defLabel(const std::string &l) {
    if (chunks_.empty() && level_ < 1) { under_->defLabel(l); return; }
    chunks_.push_back(IrChunk());
    chunks_.back().label = l;
    chunks_.back().hasLabel = true;
    if (alignNext_) { chunks_.back().before.push_back([this]() { under_->loopAlign(); }); alignNext_ = false; }
    open_ = true;
    unreachable_ = false;
}

// An event lands before the code of the chunk it precedes: on the open chunk
// while nothing has been written to it, else on a fresh one.
void Optimizer::event(std::function<void()> f) {
    if (chunks_.empty()) { f(); return; }
    IrChunk &c = (open_ && chunks_.back().ins.empty()) ? chunks_.back() : chunk();
    (c.hasLabel ? c.after : c.before).push_back(std::move(f));
}

// **Which chunk each one leads to.** A chunk ends at a terminator or where a
// label or an event began the next, and falls into that next one unless it
// left by a jump or a return.

// A jump names a label; one defined elsewhere - the function was cut - is a
// target this graph cannot see.
void Optimizer::connect() {
    std::map<std::string, int> at;
    for (std::size_t k = 0; k < chunks_.size(); k++)
        if (chunks_[k].hasLabel) at[chunks_[k].label] = static_cast<int>(k);
    for (std::size_t k = 0; k < chunks_.size(); k++) {
        IrChunk &c = chunks_[k];
        c.fall = -1; c.target = -1;
        const bool next = k + 1 < chunks_.size();
        if (c.ins.empty()) { c.fall = next ? static_cast<int>(k + 1) : -2; continue; }
        const IrIns &last = c.ins.back();
        const bool jump = !last.m.empty() && last.m[0] == 'j';
        if (jump) {
            std::map<std::string, int>::const_iterator it =
                last.operands == 1 && last.a.kind == Op::Lbl ? at.find(last.a.text) : at.end();
            c.target = it == at.end() ? -2 : it->second;
        }
        if (last.m == "ret") continue;
        if (last.m != "jmp") c.fall = next ? static_cast<int>(k + 1) : -2;
    }
}

// **Liveness across the graph.** Each chunk's gen is what it reads before it
// writes, its kill what it writes whole; in = gen | (out & ~kill), out the
// union over its successors - everything where one is unseen, nothing after a return.

// Backward to a fixpoint; the flags the same way.
void Optimizer::semantics(IrIns &x) const {
    refresh(x);
    if (x.sem.cls == IrSem::Ret && !rdxLive_) {
        x.sem.use &= ~bit(kRdx);
        x.sem.fixed &= ~bit(kRdx);
    }
    /*  **A call reads the accumulator only where the ABI says so.** Described
     *  as reading every register, a call makes rax live at every point before
     *  it, and retargetDefs - which folds `mov rax,X ; mov r8,rax` into
     *  `mov r8,X` when rax is not live out - could never fire in front of one.
     *
     *  It fired anyway, by accident, for as long as the walker emitted a dead
     *  `mov $0,%rax` before every call: that redefinition ended rax's live
     *  range. Deleting the instruction was right and cost 4,336 bytes of .text
     *  in a build of Compiler++, because 851 folds per unit went with it. The
     *  Microsoft ABI passes nothing in rax, so on that target the liveness was
     *  wrong, not the fold. System V's variadic call really does read AL.
     *
     *  **And the rest of the registers, the same way.** rax was the one
     *  carved out; every other register stayed live in front of every call,
     *  so no pass could drop or fold a value before one - on Compiler++,
     *  three quarters of the lea fusions refused were refused for that. A
     *  call reads its argument registers, rsp and rbp, and what its own
     *  operand names; it writes what the convention lets a callee clobber,
     *  and what the callee preserves passes through it untouched. Memory is
     *  not tracked register by register: a call writes it, as before. */
    if (x.sem.cls == IrSem::Call) {
        unsigned args, clobber;
        if (microsoftAbi_) {
            args = bit(kRcx) | bit(kRdx) | bit(8) | bit(9);
            for (int k = 0; k < 4; k++) args |= bit(kXmm0 + k);
            clobber = bit(kRax) | bit(kRcx) | bit(kRdx) | bit(8) | bit(9) | bit(10) | bit(11);
            for (int k = 0; k < 6; k++) clobber |= bit(kXmm0 + k);
        } else {
            args = bit(kRdi) | bit(kRsi) | bit(kRdx) | bit(kRcx) | bit(8) | bit(9);
            for (int k = 0; k < 8; k++) args |= bit(kXmm0 + k);
            clobber = bit(kRax) | bit(kRcx) | bit(kRdx) | bit(kRsi) | bit(kRdi) |
                      bit(8) | bit(9) | bit(10) | bit(11);
            for (int k = 0; k < 16; k++) clobber |= bit(kXmm0 + k);
        }
        if (callReadsAccumulator_) args |= bit(kRax);
        x.sem.use = args | bit(kRsp) | bit(kRbp) | x.sem.callee;
        x.sem.def = clobber & ~x.sem.use;
        x.sem.part = clobber & x.sem.use;
        x.sem.fixed = x.sem.use;
    }
}

void Optimizer::flow() {
    for (IrChunk &c : chunks_) {
        unsigned live = 0, kill = 0;
        bool flags = false, fkill = false;
        for (std::size_t k = c.ins.size(); k-- > 0;) {
            if (!c.ins[k].semValid) semantics(c.ins[k]);
            const IrSem &s = c.ins[k].sem;
            live = s.use | (live & ~s.def);
            kill |= s.def;
            flags = s.flagsUse || (flags && !s.flagsDef);
            fkill = fkill || s.flagsDef;
        }
        c.gen = live; c.kill = kill; c.flagsGen = flags; c.flagsKill = fkill;
        c.liveIn = live; c.flagsIn = flags;
    }
    for (int round = 0; round < 64; round++) {
        bool changed = false;
        for (std::size_t k = chunks_.size(); k-- > 0;) {
            IrChunk &c = chunks_[k];
            unsigned out = 0;
            bool fout = false;
            if (c.fall == -2 || c.target == -2) { out = kAll; fout = true; }
            if (c.fall >= 0)   { out |= chunks_[c.fall].liveIn;   fout = fout || chunks_[c.fall].flagsIn; }
            if (c.target >= 0) { out |= chunks_[c.target].liveIn; fout = fout || chunks_[c.target].flagsIn; }
            if (!c.ins.empty() && c.ins.back().sem.cls == IrSem::Call) fout = false;
            unsigned in = c.gen | (out & ~c.kill);
            bool fin = c.flagsGen || (fout && !c.flagsKill);
            if (out != c.liveOut || in != c.liveIn || fout != c.flagsOut || fin != c.flagsIn) changed = true;
            c.liveOut = out; c.liveIn = in; c.flagsOut = fout; c.flagsIn = fin;
        }
        if (!changed) break;
    }
}

// **-O2 unrolls a leaf loop by two**, as cl's /Ot does: the walker's `begin`
// chunk (the condition, ending in the branch out), a body with no label, call
// or jump, the `step` chunk if any, and the `jmp begin` - copied once after the latch.

// Labels are left out of the copy, so each pass round the back edge does two
// iterations and one jump; the copy keeps the exit branch, so the trip count
// need not be known. A body with a label would need its exception ranges rebuilt.

// Verified on the box (297/297, 67/67) and measured at no gain - see the study,
// section 6 - so flush() does not call it yet; it waits for locals in registers.
bool Optimizer::unroll() {
    const std::size_t kMost = 64;
    bool any = false;
    for (std::size_t h = 0; h < chunks_.size(); h++) {
        const IrChunk &head = chunks_[h];
        if (!head.hasLabel || head.ins.empty()) continue;
        const std::string::size_type at = head.label.find(".begin.");
        if (at == std::string::npos) continue;
        const std::string step = head.label.substr(0, at) + ".step." + head.label.substr(at + 7);
        const IrIns &exit = head.ins.back();
        if (exit.m.empty() || exit.m[0] != 'j' || exit.m == "jmp" ||
            exit.operands != 1 || exit.a.kind != Op::Lbl) continue;

        // The latch, and everything between must be plain.
        std::size_t t = h + 1, size = head.ins.size();
        bool ok = true;
        for (; t < chunks_.size(); t++) {
            const IrChunk &c = chunks_[t];
            if (c.hasLabel && c.label != step) { ok = false; break; }
            size += c.ins.size();
            if (size > kMost) { ok = false; break; }
            bool last = false;
            for (std::size_t k = 0; k < c.ins.size(); k++) {
                const IrIns &i = c.ins[k];
                if (!i.semValid) refresh(const_cast<IrIns &>(i));
                const IrSem::Class cls = i.sem.cls;
                if (cls == IrSem::Unknown || cls == IrSem::Call || cls == IrSem::Ret) { ok = false; break; }
                if (cls == IrSem::Jump) {
                    if (k + 1 != c.ins.size() || i.m != "jmp" || i.operands != 1 ||
                        i.a.kind != Op::Lbl || i.a.text != head.label) { ok = false; break; }
                    last = true;
                }
            }
            if (!ok || last) break;
        }
        if (!ok || t >= chunks_.size()) continue;
        for (std::size_t k = h + 1; k < t && ok; k++)
            for (std::size_t j = 0; j < chunks_[k].ins.size(); j++)
                if (chunks_[k].ins[j].sem.cls == IrSem::Jump) ok = false;
        if (!ok) continue;

        // The copies: the head without its label, the rest without theirs,
        // the latch's jump moved from the original to the copy.
        std::vector<IrChunk> copies;
        for (std::size_t k = h; k <= t; k++) {
            IrChunk c;
            c.ins = chunks_[k].ins;
            copies.push_back(c);
        }
        chunks_[t].ins.pop_back();
        chunks_.insert(chunks_.begin() + static_cast<long>(t) + 1, copies.begin(), copies.end());
        h = t + copies.size();
        any = true;
    }
    return any;
}

// **How wide an access to memory is**, from the mnemonic's suffix where it
// carries one, else from the register on the other side. A width nobody can
// name disqualifies the slot rather than guessing at it.
static int accessWidth(const IrIns &x, const IrOp &mem) {
    struct Suffix { const char *m; int w; };
    static const Suffix kSuffix[] = {
        { "movslq", 4 }, { "movsbq", 1 }, { "movswq", 2 },
        { "movzbq", 1 }, { "movzwq", 2 }, { "movzbl", 1 }, { "movzwl", 2 },
        { "movsbl", 1 }, { "movswl", 2 }, { "movsbw", 1 }, { "movzbw", 1 },
        { "movb", 1 }, { "movw", 2 }, { "movl", 4 }, { "movq", 8 },
        { "testb", 1 }, { "testw", 2 }, { "testl", 4 }, { "testq", 8 },
        { "addl", 4 }, { "cmpl", 4 }, { "incl", 4 }, { "decl", 4 },
        { "incq", 8 }, { "decq", 8 },
        { "push", 8 }, { "pushq", 8 }, { "pop", 8 }, { "popq", 8 },
    };
    for (const Suffix &e : kSuffix)
        if (x.m == e.m) return e.w;
    // The other operand names the width when it is a register.
    for (int k = 0; k < x.operands; k++) {
        const IrOp &o = k == 0 ? x.a : x.b;
        if (&o == &mem || o.kind != Op::Reg) continue;
        int w;
        const int r = regLookup(o.text, w);
        if (r < 0 || r >= kGprCount || w == 0) return 0;
        return w;
    }
    return 0;
}

// **A local kept in a register instead of its frame slot**, which is the one
// thing the walker's code most wants: every variable it writes is a load and
// a store, and cl's /O2 is two to three times faster on a loop for this reason.

// A slot qualifies when every rbp-relative operand that touches it names it
// exactly and at its own width: a `lea` of it, an access of another width, or
// one reaching into its middle all disqualify.

// The levels part over how many registers to keep - see below.
unsigned Optimizer::promote() {
    // The epilogue must be the shape placeRestore() knows, or the registers
    // could not be brought back: nothing is promoted then.
    bool epilogue = false;
    for (const IrChunk &c : chunks_)
        for (std::size_t k = 0; k < c.ins.size(); k++) {
            if (c.ins[k].dead || c.ins[k].m != "ret") continue;
            for (std::size_t j = k; j-- > 0;) {
                if (c.ins[j].dead) continue;
                const IrIns &t = c.ins[j];
                epilogue = t.m == "leave" ||
                           (isMov64(t) && isReg(t.a, "%rbp") && isReg(t.b, "%rsp")) ||
                           (t.m == "pop" && t.operands == 1 && isReg(t.a, "%rbp"));
                break;
            }
        }
    if (!epilogue) return 0;

    std::map<long long, int> width;      // candidate slot -> the width it must be
    for (std::size_t i = 0; i < scalars_.size(); i++)
        width[scalars_[i].first] = scalars_[i].second;
    std::map<long long, long> count;
    std::map<long long, bool> refused;

    for (IrChunk &c : chunks_)
        for (IrIns &x : c.ins) {
            if (x.dead) continue;
            if (!x.semValid) semantics(x);
            for (int k = 0; k < x.operands; k++) {
                const IrOp &o = k == 0 ? x.a : x.b;
                if (o.text != "%rbp") continue;
                const long long d = o.hasDisp ? o.disp : 0;
                if (o.kind != Op::Mem) { refused[d] = true; continue; }
                // An access that starts inside a candidate rather than at it.
                for (std::map<long long, int>::const_iterator it = width.begin();
                     it != width.end(); ++it)
                    if (d > it->first && d < it->first + it->second) refused[it->first] = true;
                std::map<long long, int>::const_iterator it = width.find(d);
                if (it == width.end()) continue;
                // `lea` takes the address; any other mnemonic must name a width.
                if (x.m == "lea" || accessWidth(x, o) != it->second || x.sem.cls == IrSem::Unknown)
                    refused[d] = true;
                else
                    count[d]++;
            }
        }

    std::vector<std::pair<long, long long> > best;
    for (std::map<long long, long>::const_iterator it = count.begin(); it != count.end(); ++it)
        if (!refused[it->first]) best.push_back(std::make_pair(it->second, it->first));
    if (best.empty()) return 0;
    std::sort(best.begin(), best.end());
    std::reverse(best.begin(), best.end());

    // **The same at both levels, and that was measured, not assumed.** A
    // register costs a save and a restore that every call pays, on every
    // path; the count credits a use on any path, and it includes the
    // parameter's home store, so a slot of one or two uses gains nothing.
    // -O2 once took five registers from one use up: on Compiler++ that ran
    // 1,582 ms against 1,483 for two from four uses up, and lost to -O1 -
    // a program of small hot functions calls far more than it loops. Five
    // won only a kernel with six live scalars (136 against 155 ms). A
    // bigger budget wants uses weighted by loop depth first.
    static const int kHome[] = { 1, 12, 13, 14, 15 };
    const std::size_t most = 2;
    const long least = 4;
    std::map<long long, int> home;
    unsigned saved = 0;
    for (std::size_t i = 0; i < best.size() && i < most; i++) {
        if (best[i].first < least) break;
        home[best[i].second] = kHome[i];
        saved |= bit(kHome[i]);
    }
    if (home.empty()) return 0;

    for (IrChunk &c : chunks_)
        for (IrIns &x : c.ins) {
            if (x.dead) continue;
            bool touched = false;
            for (int k = 0; k < x.operands; k++) {
                IrOp &o = k == 0 ? x.a : x.b;
                if (o.kind != Op::Mem || o.text != "%rbp") continue;
                std::map<long long, int>::const_iterator it =
                    home.find(o.hasDisp ? o.disp : 0);
                if (it == home.end()) continue;
                o.kind = Op::Reg;
                o.text = regName(it->second, width[o.hasDisp ? o.disp : 0]);
                o.disp = 0;
                o.hasDisp = false;
                touched = true;
            }
            if (touched) refresh(x);
        }
    return saved;
}

// **The restore goes where rbp still addresses the frame**, which is before
// the teardown: the chunk holding the epilogue is split there, and the tail
// carries the call as the event that precedes it.
void Optimizer::placeRestore() {
    for (std::size_t ci = 0; ci < chunks_.size(); ci++) {
        IrChunk &c = chunks_[ci];
        std::size_t at = c.ins.size();
        for (std::size_t k = 0; k < c.ins.size(); k++) {
            const IrIns &t = c.ins[k];
            if (t.dead) continue;
            if (t.m == "leave" || (isMov64(t) && isReg(t.a, "%rbp") && isReg(t.b, "%rsp")) ||
                (t.m == "lea" && t.operands == 2 && t.a.kind == Op::Mem &&
                 t.a.text == "%rbp" && isReg(t.b, "%rsp")))
                { at = k; break; }
        }
        if (at == c.ins.size()) continue;
        IrChunk tail;
        tail.before.push_back([this]() { under_->restoreSaved(); });
        tail.ins.assign(c.ins.begin() + static_cast<long>(at), c.ins.end());
        c.ins.resize(at);
        chunks_.insert(chunks_.begin() + static_cast<long>(ci) + 1, tail);
        return;
    }
}

void Optimizer::replay() {
    for (IrChunk &c : chunks_) {
        for (std::function<void()> &f : c.before) f();
        if (c.hasLabel) under_->defLabel(c.label);
        for (std::function<void()> &f : c.after) f();
        for (const IrIns &i : c.ins) {
            switch (i.operands) {
            case 0: under_->ins(i.m); break;
            case 1: under_->ins(i.m, i.a.view()); break;
            default: under_->ins(i.m, i.a.view(), i.b.view()); break;
            }
        }
    }
    chunks_.clear();
    open_ = false;
}

// The text is about to be read, cut, or continued by something that cannot
// stay inside the function: what is held is optimized - twice, since a chunk
// that reads less leaves less live for the ones before it - and written out.
void Optimizer::flush() {
    unsigned saved = 0;
    if (level_ >= 1 && !chunks_.empty()) {
        connect();
        for (int round = 0; round < 2; round++) {
            flow();
            for (IrChunk &c : chunks_) {
                if (c.ins.empty()) continue;
                run_.swap(c.ins);
                initLive_ = c.liveOut;
                initFlags_ = c.flagsOut;
                optimize();
                run_.swap(c.ins);
            }
            // After the first round: the locals into registers, with the
            // whole body in hand and never where there are handlers - a
            // Windows funclet reads the parent's slots and would read a stale one.
            if (round == 0 && prologueHeld_ && ending_ && heldLsda_.empty() && !scalars_.empty())
                saved = promote();
            // unroll() would go here, after the first round and before the
            // second; docs/O1-O2-STUDY-2026-09-21.md section 6 measured it at
            // nothing while every local is a load and a store, so it waits.
        }
        if (saved != 0) placeRestore();
    }
    if (prologueHeld_) {
        under_->prologue(heldFrame_, heldLsda_, saved, heldOutgoing_);
        prologueHeld_ = false;
    }
    replay();
    unreachable_ = false;
}

void Optimizer::interrupt() { flush(); }

// ---------------------------------------------------------------------------
// The passes.

void Optimizer::optimize() {
    peephole();
    for (int round = 0; round < 4; round++) {
        bool changed = false;
        analyse();
        if (dropExtensions()) { changed = true; compact(); analyse(); }
        if (foldAddIntoAddress()) { changed = true; compact(); analyse(); }
        if (fuseAddToLea())   { changed = true; compact(); analyse(); }
        if (fuseLeas())       { changed = true; compact(); analyse(); }
        if (sinkFrameLeas())  { changed = true; compact(); analyse(); }
        if (pairStack())      { changed = true; compact(); analyse(); }
        if (retargetDefs())   { changed = true; compact(); analyse(); }
        if (renameThroughPair()) { changed = true; compact(); analyse(); }
        if (foldCompareBranch()) { changed = true; compact(); analyse(); }
        if (moveSourceReads())   { changed = true; compact(); analyse(); }
        if (dropDeadDefs())      { changed = true; compact(); analyse(); }
        if (propagateCopies()){ changed = true; compact(); }
        if (!changed) break;
    }
    analyse();
    shorten();
    compact();
}

void Optimizer::compact() {
    kept_.clear();
    kept_.reserve(run_.size());
    for (IrIns &i : run_)
        if (!i.dead) kept_.push_back(std::move(i));
    run_.swap(kept_);
}

// **What each instruction does, and what is live after it.** Backward over
// the run, from what the graph says follows it - see flow().
void Optimizer::analyse() {
    const std::size_t n = run_.size();
    liveOut_.resize(n);
    flagsLiveOut_.resize(n);
    for (std::size_t i = 0; i < n; i++)
        if (!run_[i].semValid) semantics(run_[i]);

    unsigned live = initLive_;
    bool flags = initFlags_;
    for (std::size_t k = n; k-- > 0;) {
        const IrSem &s = run_[k].sem;
        liveOut_[k] = live;
        flagsLiveOut_[k] = flags ? 1 : 0;
        live = s.use | (live & ~s.def);
        flags = s.flagsUse || (flags && !s.flagsDef);
    }
}

// **Each pattern is between an instruction and the last one kept**, which is
// what lets a chain fall: a store and two reloads lose both reloads. The first
// three never fire on what the walker writes today; the fourth is its idiom.
void Optimizer::peephole() {
    std::vector<IrIns> &kept = kept_;
    kept.clear();
    kept.reserve(run_.size());
    for (IrIns &i : run_) {
        // mov %r, %r: a 64-bit self-move does nothing.
        if (isMov64(i) && i.a.kind == Op::Reg && i.b.kind == Op::Reg &&
            i.a.text == i.b.text && is64Gpr(i.a.text)) {
            removed_++;
            continue;
        }
        if (!kept.empty() && isMemToReg(i)) {
            const IrIns &p = kept.back();
            // mov %r, m ; mov m, %r: the reload reads back what %r still holds.
            if (isRegToMem(p) && p.a.text == i.b.text && p.b.same(i.a)) {
                removed_++;
                continue;
            }
            // mov m, %r ; mov m, %r: the second load reads what the first did,
            // unless %r is the address it reads through.
            if (isMemToReg(p) && p.a.same(i.a) && p.b.text == i.b.text &&
                i.a.text.find(i.b.text) == std::string::npos) {
                removed_++;
                continue;
            }
        }
        // push %x ; pop %y is mov %x, %y, and nothing at all when %y is %x: rsp
        // and the flags end as they began, and the word written below rsp is
        // one nothing reads - the frame is addressed from rbp, the stack upward.

        // **At both levels, though it looks like a size-for-speed trade.** The
        // pair is two bytes and the mov three, so it was gated to -O2 once and
        // measured: .text came out 51,408 bytes LARGER at -O1, not smaller.

        // It is an enabling transformation - once the pair is a mov, copy
        // propagation sees through it and usually deletes it. Withholding it
        // costs both size and speed, so it is not a knob.

        // What is: docs/O1-O2-STUDY-2026-09-21.md, where cl's -O2 proved to
        // be exactly -O1 -Oi -Ot - unrolled loops, inline block moves and
        // aligned loop heads - and TI's levels proved to be amounts instead.
        if (!kept.empty() && isStackOp(i, "pop") && isStackOp(kept.back(), "push")) {
            IrIns &p = kept.back();
            if (p.a.text == i.a.text) {
                kept.pop_back();
                removed_ += 2;
            } else {
                p.m = "mov";
                p.operands = 2;
                p.b = i.a;
                p.semValid = false;
                removed_++;
            }
            continue;
        }
        kept.push_back(std::move(i));
    }
    run_.swap(kept);
}

// **A sign- or zero-extension of a register that already is one.** The
// walker widens every 32-bit result with `movslq %eax, %rax` and does not
// remember having just done so; the state here does.

// Forward over the run, with what is known of each register cleared by any
// write to it.
bool Optimizer::dropExtensions() {
    enum { kSext32 = 1, kZext8 = 2, kZext16 = 4 };
    unsigned char known[kGprCount] = { 0 };
    bool changed = false;
    for (std::size_t i = 0; i < run_.size(); i++) {
        IrIns &x = run_[i];
        const IrSem &s = run_[i].sem;
        if (s.cls == IrSem::Move && x.a.kind == Op::Reg && x.b.kind == Op::Reg) {
            int wa, wb;
            int ra = regLookup(x.a.text, wa);
            int rb = regLookup(x.b.text, wb);
            if (ra >= 0 && ra == rb && rb < kGprCount && (wb == 8 || wb == 4)) {
                bool noop = false;
                if (x.m == "movslq" && wa == 4 && wb == 8) noop = (known[rb] & kSext32) != 0;
                else if ((x.m == "movzbq" || x.m == "movzbl") && wa == 1)
                    noop = (known[rb] & kZext8) != 0;
                else if ((x.m == "movzwq" || x.m == "movzwl") && wa == 2)
                    noop = (known[rb] & kZext16) != 0;
                if (noop) {
                    x.dead = true;
                    removed_++;
                    changed = true;
                    continue;
                }
            }
        }
        if (s.cls == IrSem::Unknown) {
            for (int r = 0; r < kGprCount; r++) known[r] = 0;
            continue;
        }
        const unsigned written = s.def | s.part;
        for (int r = 0; r < kGprCount; r++)
            if (written & bit(r)) known[r] = 0;
        if (s.cls == IrSem::Cltq) {
            known[kRax] = kSext32;
        } else if (s.cls == IrSem::Move && s.dstReg >= 0 && s.dstReg < kGprCount) {
            unsigned char k = 0;
            const std::string &m = x.m;
            if (m == "movslq" || m == "movsbq" || m == "movswq") {
                k = kSext32;
            } else if (m == "movzbq" || m == "movzbl") {
                k = kSext32 | kZext8 | kZext16;
            } else if (m == "movzwq" || m == "movzwl") {
                k = kSext32 | kZext16;
            } else if ((m == "mov" || m == "movq" || m == "movl" || m == "movabs") &&
                       x.a.kind == Op::Imm && x.a.immNumeric) {
                if (!x.a.immNeg) {
                    if (x.a.uimm < 128) k = kSext32 | kZext8 | kZext16;
                    else if (x.a.uimm < 65536) k = kSext32 | kZext16;
                    else if (x.a.uimm < (1ull << 31)) k = kSext32;
                } else if (s.dstWidth == 8 && x.a.uimm <= (1ull << 31)) {
                    k = kSext32;
                }
            } else if ((m == "mov" || m == "movq") && x.a.kind == Op::Reg && s.dstWidth == 8) {
                int wa;
                int ra = regLookup(x.a.text, wa);
                if (ra >= 0 && ra < kGprCount && wa == 8) k = known[ra];
            }
            known[s.dstReg] = k;
        }
    }
    return changed;
}

// **`add $N,%rD` in front of a use of `(%rD)`** belongs in the displacement:
// `add rax,113 ; movsx rax, BYTE PTR [rax]` is `movsx rax, BYTE PTR [rax+113]`.
// The walker writes the add because it computes an address a field at a time -
// a base loaded, an offset added, the access made - where cl writes the access
// alone. Fable measured 4,616 of the narrow form in one build of Compiler++
// and `add` 28,341 times against cl's 7,040.
//
// Two things have to be dead, and both are easy to miss. The **flags**, since
// add writes them and the fold deletes that write. And **%rD's raised value**:
// after the fold %rD still holds what it held before the add, so the use must
// either overwrite it or be the last reader.
bool Optimizer::foldAddIntoAddress() {
    bool changed = false;
    for (std::size_t i = 0; i + 1 < run_.size(); i++) {
        IrIns &a = run_[i];
        if (a.dead || a.m != "add" || a.operands != 2) continue;
        if (a.a.kind != Op::Imm || !a.a.immNumeric) continue;
        int r;
        if (!isGpr64(a.b, r) || !allocatable(r)) continue;
        if (flagsLiveOut_[i]) continue;

        long long n = a.a.immNeg ? -static_cast<long long>(a.a.uimm)
                                 : static_cast<long long>(a.a.uimm);

        IrIns &x = run_[i + 1];
        if (x.dead) continue;
        if (x.sem.cls == IrSem::Unknown) continue;
        // A push or pop moves rsp around the access, as fuseLeas says.
        if (r == kRsp) continue;

        IrOp *mo = nullptr;
        if (x.operands >= 1 && x.a.kind == Op::Mem && x.a.text == a.b.text) mo = &x.a;
        else if (x.operands == 2 && x.b.kind == Op::Mem && x.b.text == a.b.text) mo = &x.b;
        if (mo == nullptr) continue;
        // Both operands through the same register would need the fold twice.
        if (x.operands == 2 && x.a.kind == Op::Mem && x.b.kind == Op::Mem &&
            x.a.text == a.b.text && x.b.text == a.b.text) continue;

        long long d = (mo->hasDisp ? mo->disp : 0) + n;
        if (d > 0x7fffffffLL || d < -0x80000000LL) continue;

        const IrOp saved = *mo;
        mo->disp = d;
        mo->hasDisp = d != 0;
        const IrSem t = describe(x);
        // **Not fuseLeas's test, and the difference matters.** There the base
        // register vanishes from the instruction, so any remaining use of it
        // forbids the fold. Here the base stays and only the displacement
        // moves, so the instruction still reads %rD - and should. What must
        // not survive is %rD's *raised* value: the use has to overwrite it or
        // be the last reader.
        const bool dead = (t.def & bit(r)) != 0 || (liveOut_[i + 1] & bit(r)) == 0;
        if (t.cls == IrSem::Unknown || !dead) {
            *mo = saved;
            continue;
        }
        x.sem = t;
        x.semValid = true;
        a.dead = true;
        removed_++;
        changed = true;
    }
    return changed;
}

// **`mov %rB,%rD ; add $N,%rD` is `lea N(%rB),%rD`**, and spelled that way
// fuseLeas can go on to put the address inside the instruction that uses it -
// which is the whole point. The walker writes the pair because it computes a
// base into a register and then adds the field's offset; cl writes the access
// itself, `cmp BYTE PTR [rcx+113], bl` where this wrote three instructions.
//
// **The add's flags must be dead**, because lea sets none. That is the only
// thing the rewrite changes about the machine's state, and the one thing a
// reader would not think to check.
bool Optimizer::fuseAddToLea() {
    bool changed = false;
    for (std::size_t i = 0; i + 1 < run_.size(); i++) {
        IrIns &m = run_[i];
        if (m.dead || !isMov64(m)) continue;
        int rb, rd;
        if (!isGpr64(m.a, rb) || !isGpr64(m.b, rd)) continue;
        if (rb == rd || !allocatable(rd)) continue;

        IrIns &a = run_[i + 1];
        if (a.dead || a.m != "add" || a.operands != 2) continue;
        if (a.a.kind != Op::Imm || !a.a.immNumeric) continue;
        int rt;
        if (!isGpr64(a.b, rt) || rt != rd) continue;
        if (flagsLiveOut_[i + 1]) continue;

        long long n = a.a.immNeg ? -static_cast<long long>(a.a.uimm)
                                 : static_cast<long long>(a.a.uimm);
        if (n > 0x7fffffffLL || n < -0x80000000LL) continue;

        // rsp and rbp are the frame; sinkFrameLeas has its own rules for those
        // and this pass must not write a lea underneath it.
        if (rb == kRsp) continue;

        // The source register's name becomes the address's base; nothing else
        // about the operand changes, which is why it is built by mutating a
        // copy rather than made fresh.
        IrOp addr = m.a;
        addr.kind = Op::Mem;
        addr.disp = n;
        addr.hasDisp = n != 0;
        m.m = "lea";
        m.a = addr;
        refresh(m);
        a.dead = true;
        removed_++;
        changed = true;
    }
    return changed;
}

// **`lea M, %r` followed by a use of memory at (%r)**: the address goes into
// the operand and the lea goes, when %r is read nowhere else in that
// instruction and its value is dead afterwards.

// Dead because that instruction overwrites it, or because nothing later
// reads it before writing it.
bool Optimizer::fuseLeas() {
    bool changed = false;
    for (std::size_t i = 0; i + 1 < run_.size(); i++) {
        IrIns &l = run_[i];
        if (l.dead || l.m != "lea" || l.operands != 2) continue;
        if (l.a.kind != Op::Mem && l.a.kind != Op::Rip) continue;
        int r;
        if (!isGpr64(l.b, r) || !allocatable(r)) continue;

        IrIns &x = run_[i + 1];
        if (x.dead) continue;
        const IrSem &s = run_[i + 1].sem;
        if (s.cls == IrSem::Unknown) continue;

        IrOp *mo = nullptr;
        if (x.operands >= 1 && x.a.kind == Op::Mem && x.a.text == l.b.text) mo = &x.a;
        else if (x.operands == 2 && x.b.kind == Op::Mem && x.b.text == l.b.text) mo = &x.b;
        if (mo == nullptr) continue;

        IrOp fused = l.a;
        if (l.a.kind == Op::Rip) {
            // The spelling of a rip-relative operand carries no displacement.
            if (mo->hasDisp && mo->disp != 0) continue;
        } else {
            // A push or pop moves rsp before or after the access; keep clear.
            if (l.a.text == "%rsp" && (s.cls == IrSem::Push || s.cls == IrSem::Pop)) continue;
            long long d = (l.a.hasDisp ? l.a.disp : 0) + (mo->hasDisp ? mo->disp : 0);
            if (d > 0x7fffffffLL || d < -0x80000000LL) continue;
            fused.disp = d;
            fused.hasDisp = d != 0;
        }

        // The instruction as it would be, so its reads are the real ones.
        const IrOp saved = *mo;
        *mo = fused;
        const IrSem t = describe(x);
        const bool dead = (t.def & bit(r)) != 0 || (liveOut_[i + 1] & bit(r)) == 0;
        if (t.cls == IrSem::Unknown || (t.use & bit(r)) != 0 || !dead) {
            *mo = saved;
            continue;
        }
        x.sem = t;
        x.semValid = true;
        l.dead = true;
        removed_++;
        changed = true;
    }
    return changed;
}

// **A push and the pop that takes it back**, with nothing between that moves
// rsp or reads the stack: the same register both times, and both go; another
// register untouched between, and the push is a move to it.

// Otherwise the pushed register intact until the pop, and the pop is a move
// from it. The word below rsp is one nothing reads, as peephole() says.

// **The address of a frame slot, taken once and used as a base several
// times.** `lea D(%rbp), %r ; ... k(%r) ...` is the walker's `i++` idiom - it
// wants the address twice - and fuseLeas takes only a single use.

// Every use is rewritten to `D+k(%rbp)` and the lea goes, so the slot is
// reached by its own displacement: without this, promote() would see an
// address taken where none really is.
bool Optimizer::sinkFrameLeas() {
    bool changed = false;
    const std::size_t n = run_.size();
    std::vector<std::size_t> uses;
    for (std::size_t i = 0; i + 1 < n; i++) {
        IrIns &l = run_[i];
        if (l.dead || l.m != "lea" || l.operands != 2) continue;
        if (l.a.kind != Op::Mem || l.a.text != "%rbp") continue;
        int r;
        if (!isGpr64(l.b, r) || !allocatable(r)) continue;
        const long long base = l.a.hasDisp ? l.a.disp : 0;

        uses.clear();
        bool ok = true, dead = false;
        for (std::size_t j = i + 1; j < n && ok; j++) {
            if (j - i > kScanLimit) { ok = false; break; }
            IrIns &x = run_[j];
            if (x.dead) continue;
            const IrSem &s = x.sem;
            if (s.cls == IrSem::Unknown || s.cls == IrSem::Call) { ok = false; break; }
            if (s.use & bit(r)) {
                // Only as a memory base, and only where the displacements add.
                for (int k = 0; k < x.operands && ok; k++) {
                    const IrOp &o = k == 0 ? x.a : x.b;
                    int w;
                    if (!namesReg(o, r, w)) continue;
                    if (o.kind != Op::Mem) { ok = false; break; }
                    const long long d = base + (o.hasDisp ? o.disp : 0);
                    if (d > 0x7fffffffLL || d < -0x80000000LL) ok = false;
                }
                if (!ok) break;
                uses.push_back(j);
            }
            if (s.def & bit(r)) { dead = true; break; }
            if (s.part & bit(r)) { ok = false; break; }
        }
        if (!ok || uses.empty()) continue;
        if (!dead && (liveOut_[n - 1] & bit(r))) continue;

        for (std::size_t j : uses) {
            IrIns &x = run_[j];
            for (int k = 0; k < x.operands; k++) {
                IrOp &o = k == 0 ? x.a : x.b;
                int w;
                if (o.kind != Op::Mem || !namesReg(o, r, w)) continue;
                o.disp = base + (o.hasDisp ? o.disp : 0);
                o.hasDisp = o.disp != 0;
                o.text = "%rbp";
            }
            refresh(x);
        }
        l.dead = true;
        removed_++;
        changed = true;
    }
    return changed;
}

bool Optimizer::pairStack() {
    bool changed = false;
    const std::size_t n = run_.size();
    for (std::size_t i = 0; i < n; i++) {
        IrIns &p = run_[i];
        if (p.dead || !isStackOp(p, "push")) continue;
        int x;
        if (!isGpr64(p.a, x)) continue;
        bool xChanged = false;
        unsigned touched = 0;
        for (std::size_t j = i + 1; j < n && j - i <= kScanLimit; j++) {
            IrIns &q = run_[j];
            if (q.dead) continue;
            const IrSem &s = run_[j].sem;
            if (s.cls == IrSem::Unknown) break;
            if (isStackOp(q, "pop")) {
                int y;
                if (!isGpr64(q.a, y)) break;
                if (y == x) {
                    if (xChanged) break;
                    p.dead = q.dead = true;
                    removed_ += 2;
                } else if ((touched & bit(y)) == 0) {
                    p.m = "mov"; p.operands = 2; p.b = q.a;
                    q.dead = true;
                    removed_++;
                    refresh(p);
                } else if (!xChanged) {
                    q.m = "mov"; q.operands = 2; q.b = q.a; q.a = p.a;
                    p.dead = true;
                    removed_++;
                    refresh(q);
                } else {
                    break;
                }
                changed = true;
                break;
            }
            if ((s.use | s.def | s.part) & bit(kRsp)) break;
            touched |= s.use | s.def | s.part;
            if ((s.def | s.part) & bit(x)) xChanged = true;
        }
    }
    return changed;
}

// **A register written whole and at once copied elsewhere**, its first home
// dead after the copy: write it to the second home in the first place.
//   movq -8(%rbp), %rax ; mov %rax, %rdi   ->   movq -8(%rbp), %rdi
bool Optimizer::retargetDefs() {
    bool changed = false;
    for (std::size_t i = 0; i + 1 < run_.size(); i++) {
        IrIns &x = run_[i];
        const IrSem &s = run_[i].sem;
        if (x.dead || (s.cls != IrSem::Move && s.cls != IrSem::Pop)) continue;
        if (!allocatable(s.dstReg)) continue;

        IrIns &c = run_[i + 1];
        if (c.dead || !isMov64(c)) continue;
        int ra, rb;
        if (!isGpr64(c.a, ra) || !isGpr64(c.b, rb)) continue;
        if (ra != s.dstReg || !allocatable(rb) || rb == ra) continue;
        if (liveOut_[i + 1] & bit(ra)) continue;

        IrOp &dst = s.cls == IrSem::Pop ? x.a : x.b;
        dst.text = regName(rb, s.dstWidth);
        refresh(x);
        c.dead = true;
        removed_++;
        changed = true;
    }
    return changed;
}

// Whether x touches register r without naming it - cqo's rax, a shift's rcx.
static bool implicitly(const IrIns &x, const IrSem &s, int r) {
    if (((s.use | s.def | s.part) & bit(r)) == 0) return false;
    int w;
    for (int k = 0; k < x.operands; k++)
        if (namesReg(k == 0 ? x.a : x.b, r, w)) return false;
    return true;
}

// **A condition made into a number and then tested again.** `a < b` is a
// compare, a `setl %al` and a widening, because the value is an int the
// language may keep; where the branch is its only reader, the flags said it.
bool Optimizer::foldCompareBranch() {
    struct Cond { const char *set; const char *jump; const char *inverse; };
    static const Cond kConds[] = {
        { "sete", "je", "jne" },   { "setne", "jne", "je" },
        { "setl", "jl", "jge" },   { "setge", "jge", "jl" },
        { "setle", "jle", "jg" },  { "setg", "jg", "jle" },
        { "setb", "jb", "jae" },   { "setae", "jae", "jb" },
        { "setbe", "jbe", "ja" },  { "seta", "ja", "jbe" },
        { "setp", "jp", "jnp" },   { "setnp", "jnp", "jp" },
    };
    bool changed = false;
    std::vector<std::size_t> live;
    live.reserve(run_.size());
    for (std::size_t i = 0; i < run_.size(); i++)
        if (!run_[i].dead) live.push_back(i);

    for (std::size_t k = 0; k + 3 < live.size(); k++) {
        IrIns &set = run_[live[k]];
        IrIns &wide = run_[live[k + 1]];
        IrIns &test = run_[live[k + 2]];
        IrIns &jump = run_[live[k + 3]];

        const Cond *cond = nullptr;
        for (const Cond &c : kConds)
            if (set.m == c.set) { cond = &c; break; }
        if (cond == nullptr || set.operands != 1 || set.a.kind != Op::Reg) continue;
        int byteReg;
        {
            int w;
            byteReg = regLookup(set.a.text, w);
            if (byteReg < 0 || byteReg >= kGprCount || w != 1) continue;
        }

        // The widening: the byte just set, into a general register.
        if (wide.m != "movzbq" && wide.m != "movzbl") continue;
        if (wide.operands != 2 || wide.a.kind != Op::Reg || wide.b.kind != Op::Reg) continue;
        int w1, w2;
        if (regLookup(wide.a.text, w1) != byteReg || w1 != 1) continue;
        const int wideReg = regLookup(wide.b.text, w2);
        if (wideReg < 0 || wideReg >= kGprCount) continue;

        // The test against zero, and the branch that reads it.
        if (test.m != "cmp" || test.operands != 2) continue;
        const bool zero = test.a.kind == Op::Imm &&
                          ((test.a.immNumeric && !test.a.immNeg && test.a.uimm == 0) ||
                           (!test.a.immNumeric && test.a.text == "0"));
        if (!zero || test.b.kind != Op::Reg) continue;
        int w3;
        if (regLookup(test.b.text, w3) != wideReg) continue;
        if (jump.operands != 1 || jump.a.kind != Op::Lbl) continue;
        if (jump.m != "je" && jump.m != "jne") continue;

        // Nothing may want the number, or the flags the test would have left.
        const std::size_t at = live[k + 3];
        if (liveOut_[at] & (bit(byteReg) | bit(wideReg))) continue;
        if (flagsLiveOut_[at]) continue;

        jump.m = jump.m == std::string("jne") ? cond->jump : cond->inverse;
        jump.semValid = false;
        set.dead = wide.dead = test.dead = true;
        removed_ += 3;
        changed = true;
        k += 3;
    }
    return changed;
}

// **A value saved round a computation that only needed another register.**
// To compute an address into the scratch register the walker writes `push
// %rax ; ... into %rax ; mov %rax, %r10 ; pop %rax` - 1,400 times over sixteen files.

// When the middle writes %rax whole before it reads it, reads it nowhere
// implicitly, and never touches %r10 or the stack, it can be written in %r10
// from the start, and the push, the copy and the pop all go.
bool Optimizer::renameThroughPair() {
    bool changed = false;
    const std::size_t n = run_.size();
    for (std::size_t i = 0; i + 2 < n; i++) {
        IrIns &p = run_[i];
        if (p.dead || !isStackOp(p, "push")) continue;
        int x;
        if (!isGpr64(p.a, x) || !allocatable(x)) continue;

        // The middle: up to the copy out, which the pop of the same register follows.
        bool defined = false, ok = true;
        std::size_t j = i + 1;
        for (; j < n && ok; j++) {
            if (j - i > kScanLimit) { ok = false; break; }
            const IrIns &q = run_[j];
            if (q.dead) continue;
            const IrSem &s = q.sem;
            if (isStackOp(q, "pop")) break;
            if (s.cls == IrSem::Unknown || s.cls == IrSem::Call) { ok = false; break; }
            if ((s.use | s.def | s.part) & bit(kRsp)) { ok = false; break; }
            if (implicitly(q, s, x)) { ok = false; break; }
            if (!defined) {
                if ((s.use | s.part) & bit(x)) { ok = false; break; }
                if (s.def & bit(x)) defined = true;
            }
        }
        if (!ok || j >= n || !defined) continue;
        IrIns &q = run_[j];
        int y;
        if (!isGpr64(q.a, y) || y != x) continue;

        // The last of the middle is `mov x, r`, r otherwise untouched in it.
        std::size_t c = j;
        while (c > i + 1 && run_[c - 1].dead) c--;
        c--;
        if (c <= i) continue;
        IrIns &copy = run_[c];
        int ra, r;
        if (!isMov64(copy) || !isGpr64(copy.a, ra) || !isGpr64(copy.b, r)) continue;
        if (ra != x || r == x || !allocatable(r)) continue;
        for (std::size_t k = i + 1; k < c && ok; k++) {
            if (run_[k].dead) continue;
            const IrSem &s = run_[k].sem;
            if ((s.use | s.def | s.part) & bit(r)) ok = false;
            // Every naming of x must be one a rename can spell in r.
            int w;
            for (int o = 0; o < run_[k].operands && ok; o++)
                if (namesReg(o == 0 ? run_[k].a : run_[k].b, x, w) && (w == 0 || w == 16)) ok = false;
        }
        if (!ok) continue;

        for (std::size_t k = i + 1; k < c; k++) {
            IrIns &m = run_[k];
            if (m.dead) continue;
            int w;
            bool touched = false;
            for (int o = 0; o < m.operands; o++) {
                IrOp &op = o == 0 ? m.a : m.b;
                if (namesReg(op, x, w)) { op.text = regName(r, w); touched = true; }
            }
            if (touched) refresh(m);
        }
        p.dead = copy.dead = q.dead = true;
        removed_ += 3;
        changed = true;
        i = j;
    }
    return changed;
}


// **A register written and never read.** A move or lea into a general register
// that nothing reads before it is written again, with no store and no flags
// anyone wants, does nothing: the walker's post-increment keeps its old value so.
bool Optimizer::dropDeadDefs() {
    bool changed = false;
    for (std::size_t i = 0; i < run_.size(); i++) {
        IrIns &x = run_[i];
        const IrSem &s = x.sem;
        if (x.dead || s.cls != IrSem::Move || s.memWrite) continue;
        if (!allocatable(s.dstReg) || x.b.kind != Op::Reg) continue;
        if (s.flagsDef && flagsLiveOut_[i]) continue;
        if (liveOut_[i] & bit(s.dstReg)) continue;
        x.dead = true;
        removed_++;
        changed = true;
    }
    return changed;
}

// **A copy whose source is read on, until the source is written again.** After
// `mov a, b` the two hold one value, so those reads may name b instead - all
// of them, so that a is dead at the copy and retargetDefs can write b at once.

// Neither may be written between, a's own redefinition ending the range; and
// a read that only b's spelling could not take - a high byte, an implicit
// operand - stops it, as does a range that leaves the run with a still live.
bool Optimizer::moveSourceReads() {
    bool changed = false;
    const std::size_t n = run_.size();
    std::vector<std::size_t> reads;
    for (std::size_t i = 0; i + 1 < n; i++) {
        IrIns &c = run_[i];
        if (c.dead || !isMov64(c)) continue;
        int a, b;
        if (!isGpr64(c.a, a) || !isGpr64(c.b, b) || a == b) continue;
        if (!allocatable(a) || !allocatable(b)) continue;
        if (!(liveOut_[i] & bit(a))) continue;      // nothing to move

        reads.clear();
        bool ok = true, closed = false;
        for (std::size_t j = i + 1; j < n && ok; j++) {
            if (j - i > kScanLimit) { ok = false; break; }
            const IrIns &x = run_[j];
            if (x.dead) continue;
            const IrSem &s = x.sem;
            if (s.cls == IrSem::Unknown || s.cls == IrSem::Call) { ok = false; break; }
            if ((s.def | s.part) & bit(b)) { ok = false; break; }
            if (s.use & bit(b)) { ok = false; break; }
            if (s.use & bit(a)) {
                if ((s.fixed & bit(a)) || (s.part & bit(a))) { ok = false; break; }
                int w;
                for (int k = 0; k < x.operands && ok; k++) {
                    const IrOp &o = k == 0 ? x.a : x.b;
                    if (!namesReg(o, a, w)) continue;
                    if (w == 0 || w == 16) ok = false;
                    if (o.kind != Op::Reg && w != 8) ok = false;
                }
                if (!ok) break;
                reads.push_back(j);
            }
            if (s.def & bit(a)) { closed = true; break; }
        }
        if (!ok || reads.empty()) continue;
        if (!closed && (liveOut_[n - 1] & bit(a))) continue;

        for (std::size_t j : reads) {
            IrIns &x = run_[j];
            int w;
            for (int k = 0; k < x.operands; k++) {
                IrOp &o = k == 0 ? x.a : x.b;
                if (namesReg(o, a, w) && readsAt(x, x.sem, k)) o.text = regName(b, w);
            }
            refresh(x);
        }
        changed = true;
    }
    return changed;
}

// **A copy whose destination dies before the run ends, or is overwritten in
// it**: every read of the destination between reads the source instead,
// and the copy goes.

// The source must be intact at each of those reads, and each must be an
// operand a rename can reach - not an implicit one, not the shift count,
// not the destination of a read-modify-write.
bool Optimizer::propagateCopies() {
    bool changed = false;
    const std::size_t n = run_.size();
    std::vector<std::size_t> uses;
    for (std::size_t i = 0; i < n; i++) {
        IrIns &c = run_[i];
        if (c.dead || !isMov64(c)) continue;
        int ra, rb;
        if (!isGpr64(c.a, ra) || !isGpr64(c.b, rb)) continue;
        if (!allocatable(ra) || !allocatable(rb) || ra == rb) continue;

        uses.clear();
        bool ok = true, dead = false, sourceChanged = false;
        int penalty = 0;
        for (std::size_t j = i + 1; j < n && ok; j++) {
            if (j - i > kScanLimit) { ok = false; break; }
            const IrIns &x = run_[j];
            if (x.dead) continue;
            const IrSem &s = run_[j].sem;
            if (s.cls == IrSem::Unknown) { ok = false; break; }
            if (s.use & bit(rb)) {
                if (sourceChanged || (s.fixed & bit(rb))) { ok = false; break; }
                int w;
                for (int k = 0; k < x.operands; k++) {
                    const IrOp &o = k == 0 ? x.a : x.b;
                    if (!namesReg(o, rb, w) || !readsAt(x, s, k)) continue;
                    if (w == 0 || w == 16) { ok = false; break; }
                    if (o.kind != Op::Reg && w != 8) { ok = false; break; }
                    if (w != 8 && needsRex(ra, w) && !needsRex(rb, w)) penalty++;
                }
                if (!ok) break;
                uses.push_back(j);
            }
            if (s.def & bit(rb)) { dead = true; break; }
            if (s.part & bit(rb)) { ok = false; break; }
            if ((s.def | s.part) & bit(ra)) sourceChanged = true;
        }
        if (!ok) continue;
        if (!dead) dead = (liveOut_[n - 1] & bit(rb)) == 0;
        if (!dead) continue;
        // Each rewritten name may grow by a REX byte; the copy is three.
        if (penalty >= 3) continue;

        for (std::size_t j : uses) {
            IrIns &x = run_[j];
            int w;
            for (int k = 0; k < x.operands; k++) {
                IrOp &o = k == 0 ? x.a : x.b;
                if (namesReg(o, rb, w) && readsAt(x, run_[j].sem, k)) o.text = regName(ra, w);
            }
            refresh(x);
        }
        c.dead = true;
        removed_++;
        changed = true;
    }
    return changed;
}

// **The same instruction in fewer bytes.** None of these changes what a
// register holds; the one that touches the flags is taken only where the
// flags are dead.
void Optimizer::shorten() {
    const std::size_t n = run_.size();
    for (std::size_t i = 0; i < n; i++) {
        IrIns &x = run_[i];
        if (x.dead) continue;
        x.semValid = false;

        // **`cmp $0,%r` is `test %r,%r`**, a byte shorter and the same flags:
        // both leave CF and OF clear and take ZF, SF and PF from the value, so
        // every condition reading them reads the same thing. cl writes `test`;
        // this wrote `cmp` 3,334 times in a build of Compiler++ against cl's
        // three. The instruction stays where it is, so nothing about liveness
        // or the flags' reach changes - only its spelling.
        // The walker writes this zero both ways - `imm(0)` at one site and
        // `immText("0")` at four - so both spellings have to be recognised or
        // the rewrite fires on a fifth of what it should.
        const bool zeroImm = x.a.kind == Op::Imm &&
            ((x.a.immNumeric && !x.a.immNeg && x.a.uimm == 0) ||
             (!x.a.immNumeric && x.a.text == "0"));
        if (x.m == "cmp" && x.operands == 2 && x.b.kind == Op::Reg && zeroImm) {
            x.m = "test";
            x.a = x.b;
            continue;
        }

        // mov $imm, %r64 with imm in [0, 2^32): movl $imm, %r32 zero-extends
        // and is two bytes shorter, five where the assembler chose movabs;
        // and zero, where nothing reads the flags, is xor %r32, %r32.
        if ((x.m == "mov" || x.m == "movq" || x.m == "movabs") && x.operands == 2 &&
            x.a.kind == Op::Imm && x.a.immNumeric) {
            int r;
            if (isGpr64(x.b, r)) {
                if (!x.a.immNeg && x.a.uimm < (1ull << 32)) {
                    if (x.a.uimm == 0 && !flagsLiveOut_[i]) {
                        x.m = "xor";
                        x.a = regOp(regName(r, 4));
                        x.b = x.a;
                    } else {
                        x.m = "movl";
                        x.b.text = regName(r, 4);
                    }
                } else if (x.m == "movabs" && x.a.immNeg && x.a.uimm <= (1ull << 31)) {
                    x.m = "mov";
                }
                continue;
            }
        }
        // lea D(b), %r ; add $n, %r is lea D+n(b), %r where nothing reads the
        // flags the add would have set: four bytes for eight.
        if (x.m == "lea" && x.operands == 2 && x.a.kind == Op::Mem && i + 1 < n) {
            IrIns &y = run_[i + 1];
            int r, r2;
            if (!y.dead && y.m == "add" && y.operands == 2 && y.a.kind == Op::Imm &&
                y.a.immNumeric && isGpr64(x.b, r) && isGpr64(y.b, r2) && r == r2 &&
                !flagsLiveOut_[i + 1] && x.a.text != x.b.text) {
                long long d = x.a.hasDisp ? x.a.disp : 0;
                long long add = y.a.immNeg ? -static_cast<long long>(y.a.uimm)
                                           : static_cast<long long>(y.a.uimm);
                if (y.a.uimm < (1ull << 31) && d + add < (1ll << 31) && d + add >= -(1ll << 31)) {
                    x.a.disp = d + add;
                    x.a.hasDisp = x.a.disp != 0;
                    y.dead = true;
                    removed_++;
                }
            }
            continue;
        }
        if (x.m == "movl" && x.operands == 2 && isZeroImm(x.a) && x.b.kind == Op::Reg &&
            !flagsLiveOut_[i]) {
            int w;
            int r = regLookup(x.b.text, w);
            if (r >= 0 && r < kGprCount && w == 4) {
                x.m = "xor";
                x.a = x.b;
            }
            continue;
        }

        // cmp $0, %r sets every flag as test %r, %r does, one byte shorter.
        if ((x.m == "cmp" || x.m == "cmpq" || x.m == "cmpl" || x.m == "cmpw" || x.m == "cmpb") &&
            x.operands == 2 && isZeroImm(x.a) && x.b.kind == Op::Reg) {
            int w;
            int r = regLookup(x.b.text, w);
            if (r >= 0 && r < kGprCount && w != 0) {
                x.m = "test" + x.m.substr(3);
                x.a = x.b;
            }
            continue;
        }

        // mov %rbp, %rsp ; pop %rbp is leave, one byte for four.
        if (x.m == "mov" && x.operands == 2 && isReg(x.a, "%rbp") && isReg(x.b, "%rsp") &&
            i + 1 < n && run_[i + 1].m == "pop" && run_[i + 1].operands == 1 &&
            isReg(run_[i + 1].a, "%rbp")) {
            x.m = "leave";
            x.operands = 0;
            run_[i + 1].dead = true;
            removed_++;
            continue;
        }
    }
}

// ---------------------------------------------------------------------------
// Everything else ends the function's text and goes through in order.

void Optimizer::functionBegin(const std::string &name, bool exported, bool mergeable) {
    interrupt(); under_->functionBegin(name, exported, mergeable);
}
// The prologue waits for the body: which registers it must save is known only
// once promote() has seen every local. The flush that ends the function
// writes it, or an earlier one - a cut - with nothing saved.
void Optimizer::prologue(int frameSize, const std::string &lsda, unsigned saved,
                         int outgoing) {
    interrupt();
    if (level_ < 1) { under_->prologue(frameSize, lsda, saved, outgoing); return; }
    prologueHeld_ = true;
    heldFrame_ = frameSize;
    heldOutgoing_ = outgoing;
    heldLsda_ = lsda;
}
void Optimizer::restoreSaved() { interrupt(); under_->restoreSaved(); }
void Optimizer::functionEnd(const std::string &name) {
    interrupt();
    ending_ = false;
    scalars_.clear();
    under_->functionEnd(name);
}
void Optimizer::fileEntry(int n, const std::string &name) { interrupt(); under_->fileEntry(n, name); }
void Optimizer::location(int file, int line, int column) {
    event([this, file, line, column]() { under_->location(file, line, column); });
}
void Optimizer::predefine(const std::vector<std::string> &names) { interrupt(); under_->predefine(names); }
void Optimizer::preamble(std::ostream &o) { interrupt(); under_->preamble(o); }
void Optimizer::postamble(std::ostream &o) { interrupt(); under_->postamble(o); }
void Optimizer::globl(const std::string &name) { interrupt(); under_->globl(name); }
void Optimizer::weakDefinition(const std::string &name) { interrupt(); under_->weakDefinition(name); }
void Optimizer::textSection() { interrupt(); under_->textSection(); }
void Optimizer::rodataSection() { interrupt(); under_->rodataSection(); }
void Optimizer::dataSection() { interrupt(); under_->dataSection(); }
void Optimizer::bssSection() { interrupt(); under_->bssSection(); }
void Optimizer::objectType(const std::string &name) { interrupt(); under_->objectType(name); }
void Optimizer::objectSize(const std::string &name, int size) { interrupt(); under_->objectSize(name, size); }
void Optimizer::align(int n) { interrupt(); under_->align(n); }
// **Before the label the loop jumps back to, and nowhere else.** The walker
// asks for it and defines that label next; as an event it would join the open
// chunk and replay ahead of the code already in it - the loop's init.
void Optimizer::loopAlign() {
    if (chunks_.empty()) { under_->loopAlign(); return; }
    alignNext_ = true;
}
void Optimizer::zero(int n) { interrupt(); under_->zero(n); }
void Optimizer::dataInt(int size, long long v) { interrupt(); under_->dataInt(size, v); }
void Optimizer::dataSym(const std::string &sym, long long off) { interrupt(); under_->dataSym(sym, off); }
void Optimizer::noteHasEh(bool yes) { interrupt(); under_->noteHasEh(yes); }
void Optimizer::initialiserEntry(const std::string &fn, bool dsoHandle) {
    interrupt(); under_->initialiserEntry(fn, dsoHandle);
}
std::string Optimizer::labelText(const std::string &l) const { return under_->labelText(l); }
void Optimizer::dataBytes(const std::string &bytes) { interrupt(); under_->dataBytes(bytes); }
