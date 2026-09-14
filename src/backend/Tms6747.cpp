#include "Tms6747.h"

#include "../Abi.h"
#include "../Ast.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ostream>
#include <string>

static int align8(int n) { return (n + 7) / 8 * 8; }

// ---- the target: sizes for a 32-bit C6000 (EABI) -------------------------
int Tms6747Target::sizeOf(Kind k) const {
    switch (k) {
    case Kind::Void:                                     return 1;
    case Kind::Bool:                                     return 1;
    case Kind::Char: case Kind::SChar: case Kind::UChar: return 1;
    case Kind::Short: case Kind::UShort:                 return 2;
    case Kind::Int: case Kind::UInt:                     return 4;
    case Kind::Long: case Kind::ULong:                   return 4;
    case Kind::LongLong: case Kind::ULongLong:           return 8;
    case Kind::Float:                                    return 4;
    case Kind::Double: case Kind::LongDouble:            return 8;
    case Kind::Pointer: case Kind::NullPtr:              return 4;
    default:
        std::fprintf(stderr, "target: no size for this type yet (tms6747)\n");
        std::exit(1);
    }
}
int Tms6747Target::alignOf(Kind k) const { return sizeOf(k); }

// ---- the backend ---------------------------------------------------------
// C6000 EABI argument registers, in order; the result comes back in A4.
static const char *const kTiIntRegs[] = {
    "A4", "B4", "A6", "B6", "A8", "B8", "A10", "B10", "A12", "B12"
};

// Set by name, as the host ABIs are. structReturnLimit 0 and aggregatesByReference
// false: every struct comes back through the pointer the caller hands over in A3 -
// TI's convention - and goes by the address of a copy (the parser's arg slot).
static Abi ti() {
    Abi a;
    a.intRegs = kTiIntRegs;             a.intCount = 10;
    a.positional = true;
    a.scratch = "A0";                   a.scratch32 = "A0";
    a.elfSymbolAttributes = true;
    return a;
}

const Abi &Tms6747Backend::abi() const {
    static const Abi kAbi = ti();
    return kAbi;
}

const char *const *Tms6747Backend::identityMacros() const {
    static const char *const kMacros[] = {
        "__tms320c6x__=1", "__TMS320C6X__=1", "__TMS320C6700__=1",
        "__TMS320C6740__=1", "__C6X__=1", "__LITTLE_ENDIAN__=1", nullptr
    };
    return kMacros;
}

std::unique_ptr<CodeGen> Tms6747Backend::codegen(std::ostream &sink, Syntax) const {
    return std::unique_ptr<CodeGen>(new Tms6747(sink, target_, abi()));
}

// ---- small helpers -------------------------------------------------------
void Tms6747::unsupported(const char *what) {
    std::fprintf(stderr, "codegen: %s is not supported yet by the tms6747 backend\n",
                 what);
    std::exit(1);
}

// A 32-bit constant in two halves: MVKL takes the low 16 (sign extended), MVKH
// the high 16. Works for any A- or B-file register.
void Tms6747::movImm(const char *reg, long long value) {
    long long v = static_cast<int>(value);
    out_ << "\tMVKL\t" << v << ", " << reg << "\n";
    out_ << "\tMVKH\t" << v << ", " << reg << "\n";
}

// A symbol as the assembler sees it. The parser names string literals
// `.L.str.N`, GNU style; a leading dot reads as a directive to the TI
// assembler, so it is dropped: `L.str.N`, beside the `L.` code labels.
std::string Tms6747::symName(const std::string &sym) {
    return sym[0] == '.' ? sym.substr(1) : sym;
}

// A symbol's address, the same two halves; the assembler and linker fill them.
void Tms6747::movSym(const char *reg, const std::string &sym) {
    out_ << "\tMVKL\t" << symName(sym) << ", " << reg << "\n";
    out_ << "\tMVKH\t" << symName(sym) << ", " << reg << "\n";
}

// dst = base + off. A large offset goes through the scratch register of the
// destination's file so the ADD stays on one side.
void Tms6747::regAdd(const char *base, int off, const char *dst) {
    if (off >= 0 && off <= 31) {
        out_ << "\tADD\t" << base << ", " << off << ", " << dst << "\n";
    } else {
        const char *tmp = dst[0] == 'B' ? "B0" : "A0";
        movImm(tmp, off);
        out_ << "\tADD\t" << base << ", " << tmp << ", " << dst << "\n";
    }
}

// B15 is the stack pointer. Small adjustments use the 5-bit immediate; larger
// ones go through a B-file scratch so the ADD/SUB stays on one register file.
void Tms6747::spAdjust(int delta) {
    if (delta == 0) return;
    int m = delta < 0 ? -delta : delta;
    const char *op = delta < 0 ? "SUB" : "ADD";
    if (m <= 31) {
        out_ << "\t" << op << "\tB15, " << m << ", B15\n";
    } else {
        movImm("B0", m);
        out_ << "\t" << op << "\tB15, B0, B15\n";
    }
}

// dst = A15 - off, where A15 is the frame pointer. dst is an A-file register.
void Tms6747::localAddr(int off, const char *dst) {
    if (off >= 0 && off <= 31) {
        out_ << "\tSUB\tA15, " << off << ", " << dst << "\n";
    } else {
        movImm("A0", off);
        out_ << "\tSUB\tA15, A0, " << dst << "\n";
    }
}

// A4 += bytes, for a member's offset.
void Tms6747::addOffset(int bytes) {
    if (bytes == 0) return;
    regAdd("A4", bytes, "A4");
}

// A struct copy, word by word then halfword and byte, each through A3 with
// the addresses formed in A0: the zero-offset forms, like every other access
// here. from and to are A-file registers other than A0 and A3.
void Tms6747::copyBlock(int size, const char *from, const char *to, int align) {
    int off = 0;
    while (off < size) {
        int step = size - off >= 4 ? 4 : size - off >= 2 ? 2 : 1;
        if (step > align) step = align;     // a struct of chars may sit anywhere
        const char *ld = step == 4 ? "LDW" : step == 2 ? "LDH" : "LDB";
        const char *st = step == 4 ? "STW" : step == 2 ? "STH" : "STB";
        regAdd(from, off, "A0");
        out_ << "\t" << ld << "\t*A0, A3\n\tNOP\t4\n";
        regAdd(to, off, "A0");
        out_ << "\t" << st << "\tA3, *A0\n";
        off += step;
    }
}

void Tms6747::push() {
    out_ << "\tSUB\tB15, 8, B15\n";
    out_ << "\tSTW\tA4, *B15\n";
}
void Tms6747::pop(const char *reg) {
    out_ << "\tLDW\t*B15, " << reg << "\n\tNOP\t4\n";
    out_ << "\tADD\tB15, 8, B15\n";
}

// ---- doubles: a register pair, and 8-byte moves through the stack --------
// A double lives in an even:odd pair, its high word in the odd register:
// A5:A4 is the accumulator, A7:A6 the second operand, B5:B4 an argument.
bool Tms6747::isDouble(const Type *t) const {
    return t->isFloating() && t->size(target_) == 8;
}
bool Tms6747::isWide(const Type *t) const {
    return (t->isFloating() || t->isInteger()) && t->size(target_) == 8;
}
std::string Tms6747::pairOf(const char *reg) {
    int n = std::atoi(reg + 1);
    return std::string(1, reg[0]) + std::to_string(n + 1) + ":" + reg;
}
// The value of type t in the accumulator, to and from the stack. A push slot
// is already 8 bytes and B15 stays 8-aligned, which STDW and LDDW need.
void Tms6747::pushValue(const Type *t) {
    if (!isWide(t)) { push(); return; }
    out_ << "\tSUB\tB15, 8, B15\n";
    out_ << "\tSTDW\tA5:A4, *B15\n";
}
void Tms6747::popValue(const Type *t, const char *reg) {
    if (!isWide(t)) { pop(reg); return; }
    out_ << "\tLDDW\t*B15, " << pairOf(reg) << "\n\tNOP\t4\n";
    out_ << "\tADD\tB15, 8, B15\n";
}
// The accumulator's value into reg (and its pair for a double).
void Tms6747::moveValue(const Type *t, const char *reg) {
    out_ << "\tMV\tA4, " << reg << "\n";
    if (isWide(t)) out_ << "\tMV\tA5, " << pairOf(reg).substr(0, pairOf(reg).find(':')) << "\n";
}
// A floating constant's bits: one word for a float, two for a double, the
// low word in reg and the high in its pair.
void Tms6747::fpConst(const Type *t, double v, const char *reg) {
    if (isDouble(t)) {
        unsigned long long bits;
        std::memcpy(&bits, &v, sizeof bits);
        movImm(reg, static_cast<long long>(bits & 0xffffffffu));
        std::string hi = pairOf(reg).substr(0, pairOf(reg).find(':'));
        movImm(hi.c_str(), static_cast<long long>(bits >> 32));
    } else {
        float f = static_cast<float>(v);
        unsigned int bits;
        std::memcpy(&bits, &f, sizeof bits);
        movImm(reg, static_cast<long long>(bits));
    }
}

// ---- addresses, loads, stores --------------------------------------------
void Tms6747::genAddr(const Expr &e) {
    if (const Var *v = dynamic_cast<const Var *>(&e)) {
        if (v->isLocal()) { localAddr(v->offset(), "A4"); return; }
        movSym("A4", v->symbol());  // a global, or a function
        return;
    }
    if (const StrLit *s = dynamic_cast<const StrLit *>(&e)) {
        movSym("A4", s->label());
        return;
    }
    if (const Unary *u = dynamic_cast<const Unary *>(&e)) {
        if (u->op() == '*') { u->operand().accept(*this); return; }
    }
    if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(&e)) {
        if (m->isBitField()) {
            std::fprintf(stderr, "codegen: '%s' is a bit-field and has no address\n",
                         m->name().c_str());
            std::exit(1);
        }
        genAddr(m->object());
        addOffset(m->offset());
        return;
    }
    // A struct-valued call or conditional: its value already is an address.
    if (const Call *c = dynamic_cast<const Call *>(&e)) {
        if (c->type()->isStructOrUnion()) { c->accept(*this); return; }
    }
    if (const Conditional *q = dynamic_cast<const Conditional *>(&e)) {
        if (q->type()->isStructOrUnion()) { q->accept(*this); return; }
    }
    unsupported("this address");
}

void Tms6747::load(const Type *t) {
    if (t->isArray() || t->isStructOrUnion()) return;   // the address is the value
    if (isWide(t)) { out_ << "\tLDDW\t*A4, A5:A4\n\tNOP\t4\n"; return; }
    int sz = t->size(target_);
    bool sign = t->isSigned(target_);
    const char *op = sz == 1 ? (sign ? "LDB" : "LDBU")
                   : sz == 2 ? (sign ? "LDH" : "LDHU")
                             : "LDW";
    out_ << "\t" << op << "\t*A4, A4\n\tNOP\t4\n";
}

void Tms6747::store(const Type *t, const char *addrReg) {
    if (isWide(t)) { out_ << "\tSTDW\tA5:A4, *" << addrReg << "\n"; return; }
    int sz = t->size(target_);
    const char *op = sz == 1 ? "STB" : sz == 2 ? "STH" : "STW";
    out_ << "\t" << op << "\tA4, *" << addrReg << "\n";
}

void Tms6747::narrowInt(const Type *t) {
    int sz = t->size(target_);
    bool sign = t->isSigned(target_);
    if (sz == 1)      out_ << (sign ? "\tEXT\tA4, 24, 24, A4\n" : "\tEXTU\tA4, 24, 24, A4\n");
    else if (sz == 2) out_ << (sign ? "\tEXT\tA4, 16, 16, A4\n" : "\tEXTU\tA4, 16, 16, A4\n");
    // 4 bytes and up: the value already fills the 32-bit register.
}

// ---- Walker hooks --------------------------------------------------------
std::string Tms6747::label(const char *kind, int id) const {
    return labelPrefix_ + kind + std::to_string(id);
}
std::string Tms6747::userLabel(const std::string &name) const {
    return "L." + functionName_ + "." + name;
}
void Tms6747::defineLabel(const std::string &l) { out_ << l << ":\n"; }
void Tms6747::jump(const std::string &l) { out_ << "\tB\t" << l << "\n\tNOP\t5\n"; }

// Only A0-A2 and B0-B2 can predicate a branch, so the truth value is moved into
// A1 first.
void Tms6747::branchIfZero(const std::string &l) {
    out_ << "\tMV\tA4, A1\n\t[!A1]\tB\t" << l << "\n\tNOP\t5\n";
}
void Tms6747::branchIfNotZero(const std::string &l) {
    out_ << "\tMV\tA4, A1\n\t[A1]\tB\t" << l << "\n\tNOP\t5\n";
}
void Tms6747::caseBranch(long long v, const std::string &l) {
    movImm("A0", v);
    out_ << "\tCMPEQ\tA0, A4, A1\n\t[A1]\tB\t" << l << "\n\tNOP\t5\n";
}
// A landing pad: the unwinder arrives with the exception in A4 and the
// selector in B4 - the parser's index of the handler type that matched, or
// 0 for a cleanup - and the frame's A15 and B15 as the body had them.
void Tms6747::landingPad(int pointerSlot, int selectorSlot) {
    localAddr(pointerSlot, "A0");
    out_ << "\tSTW\tA4, *A0\n";
    localAddr(selectorSlot, "A0");
    out_ << "\tSTW\tB4, *A0\n";
}

// The function's exception table in the emulator's fixed-width form (see
// VM6747/TMS6747.md, "Exceptions"): a row per call-site range, found by the
// unwinder by the return address of the call that threw.
void Tms6747::emitExceptionTable(const Function &fn, int frameBytes) {
    const std::vector<CallSite> &rows = callSites();
    if (rows.empty()) return;
    out_ << "\t.sect\t\".vm6747.eh\"\n\t.align\t4\n";
    for (std::size_t i = 0; i < rows.size(); i++) {
        const CallSite &c = rows[i];
        out_ << "\t.word\t" << c.begin << ", " << c.end << ", " << c.pad << ", " << frameBytes
             << ", " << (c.cleanup || c.types.empty() ? 1 : 0) << ", " << c.types.size() << "\n";
        for (std::size_t k = 0; k < c.types.size(); k++) {
            int ix = k < c.indices.size() ? c.indices[k] : static_cast<int>(k) + 1;
            out_ << "\t.word\t" << (c.types[k].empty() ? std::string("0") : c.types[k]) << ", " << ix << "\n";
        }
    }
    out_ << "\t.text\n";
    (void)fn;
}
// A4 = (value == 0) ? 1 : 0, for the value of type t in the accumulator. A
// floating zero is compared as a number, so -0.0 is zero and NaN is not.
void Tms6747::isZero(const Type *t) {
    if (isDouble(t)) {
        out_ << "\tZERO\tA7:A6\n\tCMPEQDP\tA5:A4, A7:A6, A4\n\tNOP\t1\n";
    } else if (t->isFloating()) {
        out_ << "\tZERO\tA6\n\tCMPEQSP\tA4, A6, A4\n\tNOP\t1\n";
    } else if (isWide(t)) {
        out_ << "\tOR\tA4, A5, A4\n\tCMPEQ\t0, A4, A4\n";
    } else {
        out_ << "\tCMPEQ\t0, A4, A4\n";
    }
}
void Tms6747::genTruth(const Expr &e) {
    e.accept(*this);
    isZero(e.type());
    out_ << "\tXOR\t1, A4, A4\n";  // A4 = (value != 0) ? 1 : 0
}

// ---- expressions ---------------------------------------------------------
void Tms6747::visit(const Num &n) {
    if (n.type()->isFloating()) { fpConst(n.type(), static_cast<double>(n.dvalue()), "A4"); return; }
    movImm("A4", n.value());
    if (isWide(n.type())) movImm("A5", n.value() >> 32);
}
void Tms6747::visit(const Var &n) { genAddr(n); load(n.type()); }

// ---- bit-fields ----------------------------------------------------------
// A bit-field is width bits from bitOffset up in the unit at the member's
// offset. EXT/EXTU read it in one instruction (up to the top of the word,
// back down); a write is CLR, EXTU + SHL of the new value, OR, store.
void Tms6747::bitFieldUnitAddr(const MemberAccess &m) {
    genAddr(m.object());
    addOffset(m.offset());
}
void Tms6747::bitFieldExtract(const MemberAccess &m) {    // unit in A4 -> field
    if (isWide(m.type())) unsupported("a bit-field in a 64-bit unit");
    int left = 32 - m.bitOffset() - m.width();
    int right = 32 - m.width();
    out_ << (m.type()->isSigned(target_) ? "\tEXT\tA4, " : "\tEXTU\tA4, ")
         << left << ", " << right << ", A4\n";
}
void Tms6747::bitFieldInsert(const MemberAccess &m) {     // value A4 -> *A6
    int low = m.bitOffset(), high = m.bitOffset() + m.width() - 1;
    out_ << "\tMV\tA4, A3\n";                               // A3 = the value
    out_ << "\tMV\tA6, A4\n";
    load(m.type());                                          // A4 = the unit
    out_ << "\tCLR\tA4, " << low << ", " << high << ", A4\n";
    out_ << "\tEXTU\tA3, " << (32 - m.width()) << ", " << (32 - m.width())
         << ", A3\n";                                       // the low width bits
    if (low != 0) out_ << "\tSHL\tA3, " << low << ", A3\n";
    out_ << "\tOR\tA4, A3, A4\n";
    store(m.type(), "A6");
    bitFieldExtract(m);                     // the expression's value: the field
}

void Tms6747::visit(const Assign &n) {
    const MemberAccess *bf = dynamic_cast<const MemberAccess *>(&n.target());
    if (bf != nullptr && !bf->isBitField()) bf = nullptr;

    n.value().accept(*this);        // A4 = value (a struct's is its address)
    push();                         // save the value
    if (bf) bitFieldUnitAddr(*bf);  // A4 = the unit's address
    else    genAddr(n.target());    // A4 = address
    out_ << "\tMV\tA4, A6\n";        // A6 = address
    pop("A4");                       // A4 = value again
    if (bf) { bitFieldInsert(*bf); return; }
    if (n.type()->isStructOrUnion()) {
        copyBlock(n.type()->size(target_), "A4", "A6", n.type()->align(target_));
        out_ << "\tMV\tA6, A4\n";    // the result: the target, by address
        return;
    }
    store(n.type(), "A6");           // *A6 = A4 ; A4 stays the value (the result)
}

void Tms6747::visit(const Unary &n) {
    switch (n.op()) {
    case '+': n.operand().accept(*this); return;
    case '-':
        n.operand().accept(*this);
        if (n.type()->isFloating()) {
            // Flip the sign bit, which is right for -0.0 and NaN where 0 - x
            // would not be. The bit is in the high word of a double.
            movImm("A0", 0x80000000LL);
            out_ << "\tXOR\t" << (isDouble(n.type()) ? "A5, A0, A5" : "A4, A0, A4") << "\n";
            return;
        }
        if (isWide(n.type())) {
            // -x = ~x + 1: the low word negates, the high word inverts and
            // takes the carry, which is there exactly when the low word was 0.
            out_ << "\tCMPEQ\t0, A4, A0\n\tNEG\tA4, A4\n\tNOT\tA5, A5\n\tADD\tA5, A0, A5\n";
            return;
        }
        out_ << "\tNEG\tA4, A4\n";
        return;
    case '~':
        n.operand().accept(*this);
        out_ << "\tNOT\tA4, A4\n";
        if (isWide(n.type())) out_ << "\tNOT\tA5, A5\n";
        return;
    case '!':
        n.operand().accept(*this);
        isZero(n.operand().type());
        return;
    case '&': genAddr(n.operand()); return;
    case '*': n.operand().accept(*this); load(n.type()); return;
    default:  unsupported("this unary operator");
    }
}

void Tms6747::visit(const Binary &n) {
    if (n.op() == BinOp::LAnd || n.op() == BinOp::LOr) {
        int id = nextLabel();
        std::string sc = label("shortcut", id);
        bool isAnd = n.op() == BinOp::LAnd;
        genTruth(n.lhs());
        out_ << "\tMV\tA4, A1\n\t[" << (isAnd ? "!" : "") << "A1]\tB\t" << sc
             << "\n\tNOP\t5\n";
        genTruth(n.rhs());
        defineLabel(sc);
        return;
    }

    const Type *ot = n.lhs().type();        // the operands' common type
    n.lhs().accept(*this);          // A4 = lhs
    pushValue(ot);
    n.rhs().accept(*this);          // A4 = rhs
    moveValue(ot, "A6");             // A6 = rhs
    popValue(ot, "A4");              // A4 = lhs   (now A4=lhs, A6=rhs)

    if (ot->isFloating()) { fpBinary(n, isDouble(ot)); return; }
    if (isWide(ot)) { wideBinary(n); return; }

    bool sign = ot->isSigned(target_);
    switch (n.op()) {
    case BinOp::Add:    out_ << "\tADD\tA4, A6, A4\n";  narrowInt(n.type()); return;
    case BinOp::Sub:    out_ << "\tSUB\tA4, A6, A4\n";  narrowInt(n.type()); return;
    case BinOp::Mul:    out_ << "\tMPY32\tA4, A6, A4\n\tNOP\t3\n"; narrowInt(n.type()); return;
    case BinOp::BitAnd: out_ << "\tAND\tA4, A6, A4\n";  narrowInt(n.type()); return;
    case BinOp::BitOr:  out_ << "\tOR\tA4, A6, A4\n";   narrowInt(n.type()); return;
    case BinOp::BitXor: out_ << "\tXOR\tA4, A6, A4\n";  narrowInt(n.type()); return;
    case BinOp::Shl:    out_ << "\tSHL\tA4, A6, A4\n";  narrowInt(n.type()); return;
    case BinOp::Shr:
        out_ << (sign ? "\tSHR\tA4, A6, A4\n" : "\tSHRU\tA4, A6, A4\n");
        narrowInt(n.type());
        return;
    case BinOp::Div: case BinOp::Mod: {
        // No divide instruction: the EABI's helper takes A4 and B4 and
        // returns in A4, called like any function - an area opened, B3 set -
        // so nothing it may clobber is assumed to survive.
        const char *helper = n.op() == BinOp::Div ? (sign ? "__c6xabi_divi" : "__c6xabi_divu")
                                                  : (sign ? "__c6xabi_remi" : "__c6xabi_remu");
        spAdjust(-8);
        out_ << "\tMV\tA6, B4\n";
        call(helper);
        spAdjust(8);
        narrowInt(n.type());
        return;
    }
    case BinOp::Eq: out_ << "\tCMPEQ\tA4, A6, A4\n"; return;
    case BinOp::Ne: out_ << "\tCMPEQ\tA4, A6, A4\n\tXOR\t1, A4, A4\n"; return;
    case BinOp::Lt: out_ << (sign ? "\tCMPLT\tA4, A6, A4\n" : "\tCMPLTU\tA4, A6, A4\n"); return;
    case BinOp::Gt: out_ << (sign ? "\tCMPGT\tA4, A6, A4\n" : "\tCMPGTU\tA4, A6, A4\n"); return;
    case BinOp::Le: // !(lhs > rhs)
        out_ << (sign ? "\tCMPGT\tA4, A6, A4\n" : "\tCMPGTU\tA4, A6, A4\n") << "\tXOR\t1, A4, A4\n";
        return;
    case BinOp::Ge: // !(lhs < rhs)
        out_ << (sign ? "\tCMPLT\tA4, A6, A4\n" : "\tCMPLTU\tA4, A6, A4\n") << "\tXOR\t1, A4, A4\n";
        return;
    default: unsupported("this binary operator");
    }
}

// The C674x's own SP/DP instructions, each with its delay slots as NOPs
// (TMS6747.md has the counts); division through the EABI helpers. <= and >=
// are two compares ORed, which keeps NaN unordered where !(a > b) would not.
void Tms6747::fpBinary(const Binary &n, bool dp) {
    const char *l = dp ? "A5:A4" : "A4", *r = dp ? "A7:A6" : "A6";
    const char *sfx = dp ? "DP" : "SP";
    auto op3 = [&](const char *mnem, int slots) {
        out_ << "\t" << mnem << sfx << "\t" << l << ", " << r << ", " << l
             << "\n\tNOP\t" << slots << "\n";
    };
    auto cmp = [&](const char *mnem, const char *dst) {
        out_ << "\t" << mnem << sfx << "\t" << l << ", " << r << ", " << dst
             << "\n\tNOP\t1\n";
    };
    switch (n.op()) {
    case BinOp::Add: op3("ADD", dp ? 6 : 3); return;
    case BinOp::Sub: op3("SUB", dp ? 6 : 3); return;
    case BinOp::Mul: op3("MPY", dp ? 9 : 3); return;
    case BinOp::Div:
        spAdjust(-8);
        out_ << "\tMV\tA6, B4\n";
        if (dp) out_ << "\tMV\tA7, B5\n";
        call(dp ? "__c6xabi_divd" : "__c6xabi_divf");
        spAdjust(8);
        return;
    case BinOp::Eq: cmp("CMPEQ", "A4"); return;
    case BinOp::Ne: cmp("CMPEQ", "A4"); out_ << "\tXOR\t1, A4, A4\n"; return;
    case BinOp::Lt: cmp("CMPLT", "A4"); return;
    case BinOp::Gt: cmp("CMPGT", "A4"); return;
    case BinOp::Le: cmp("CMPLT", "A0"); cmp("CMPEQ", "A4"); out_ << "\tOR\tA0, A4, A4\n"; return;
    case BinOp::Ge: cmp("CMPGT", "A0"); cmp("CMPEQ", "A4"); out_ << "\tOR\tA0, A4, A4\n"; return;
    default: unsupported("this floating-point operator");
    }
}

// 64-bit integers in A5:A4 and A7:A6 with 32-bit instructions: carry by
// CMPLTU of the low words, MPY32U plus the cross products, division by the
// EABI's helpers, compares on the high word falling to the low on a tie.
void Tms6747::wideBinary(const Binary &n) {
    bool sign = n.lhs().type()->isSigned(target_);
    switch (n.op()) {
    case BinOp::Add:
        out_ << "\tADD\tA4, A6, A4\n\tCMPLTU\tA4, A6, A0\n";   // carry: sum < addend
        out_ << "\tADD\tA5, A7, A5\n\tADD\tA5, A0, A5\n";
        return;
    case BinOp::Sub:
        out_ << "\tCMPLTU\tA4, A6, A0\n\tSUB\tA4, A6, A4\n";   // borrow: lhs < rhs
        out_ << "\tSUB\tA5, A7, A5\n\tSUB\tA5, A0, A5\n";
        return;
    case BinOp::Mul:
        out_ << "\tMPY32U\tA4, A6, A1:A0\n\tNOP\t3\n";       // the full low product
        out_ << "\tMPY32\tA4, A7, A3\n\tNOP\t3\n\tADD\tA1, A3, A1\n";
        out_ << "\tMPY32\tA5, A6, A3\n\tNOP\t3\n\tADD\tA1, A3, A1\n";
        out_ << "\tMV\tA0, A4\n\tMV\tA1, A5\n";
        return;
    case BinOp::Div: case BinOp::Mod: {
        const char *helper = n.op() == BinOp::Div ? (sign ? "__c6xabi_divlli" : "__c6xabi_divull")
                                                  : (sign ? "__c6xabi_remlli" : "__c6xabi_remull");
        spAdjust(-8);
        out_ << "\tMV\tA6, B4\n\tMV\tA7, B5\n";
        call(helper);
        spAdjust(8);
        return;
    }
    case BinOp::BitAnd: out_ << "\tAND\tA4, A6, A4\n\tAND\tA5, A7, A5\n"; return;
    case BinOp::BitOr:  out_ << "\tOR\tA4, A6, A4\n\tOR\tA5, A7, A5\n";   return;
    case BinOp::BitXor: out_ << "\tXOR\tA4, A6, A4\n\tXOR\tA5, A7, A5\n"; return;
    case BinOp::Eq:
        out_ << "\tCMPEQ\tA4, A6, A0\n\tCMPEQ\tA5, A7, A4\n\tAND\tA0, A4, A4\n";
        return;
    case BinOp::Ne:
        out_ << "\tCMPEQ\tA4, A6, A0\n\tCMPEQ\tA5, A7, A4\n\tAND\tA0, A4, A4\n\tXOR\t1, A4, A4\n";
        return;
    case BinOp::Lt: case BinOp::Ge: {
        // lhs < rhs: high < high, or high == high and low <u low.
        out_ << (sign ? "\tCMPLT\tA5, A7, A0\n" : "\tCMPLTU\tA5, A7, A0\n");
        out_ << "\tCMPEQ\tA5, A7, A3\n\tCMPLTU\tA4, A6, A4\n\tAND\tA3, A4, A4\n\tOR\tA0, A4, A4\n";
        if (n.op() == BinOp::Ge) out_ << "\tXOR\t1, A4, A4\n";
        return;
    }
    case BinOp::Gt: case BinOp::Le: {
        out_ << (sign ? "\tCMPGT\tA5, A7, A0\n" : "\tCMPGTU\tA5, A7, A0\n");
        out_ << "\tCMPEQ\tA5, A7, A3\n\tCMPGTU\tA4, A6, A4\n\tAND\tA3, A4, A4\n\tOR\tA0, A4, A4\n";
        if (n.op() == BinOp::Le) out_ << "\tXOR\t1, A4, A4\n";
        return;
    }
    case BinOp::Shl: case BinOp::Shr: {
        // The count is in A6. A shift by 32 or more of a single word gives 0
        // (or the sign), which is what makes the splice right at a count of 0.
        int id = nextLabel();
        std::string big = label("wide", id), done = label("widend", id);
        bool left = n.op() == BinOp::Shl;
        const char *shr = sign ? "SHR" : "SHRU";
        out_ << "\tAND\tA6, 63, A6\n";
        movImm("A0", 32);
        out_ << "\tCMPLTU\tA6, A0, A1\n\t[!A1]\tB\t" << big << "\n\tNOP\t5\n";
        out_ << "\tSUB\tA0, A6, A0\n";                           // 32 - count
        if (left) {
            out_ << "\tSHL\tA5, A6, A5\n\tSHRU\tA4, A0, A3\n\tOR\tA5, A3, A5\n";
            out_ << "\tSHL\tA4, A6, A4\n";
        } else {
            out_ << "\t" << shr << "\tA5, A6, A3\n\tSHRU\tA4, A6, A4\n\tSHL\tA5, A0, A5\n";
            out_ << "\tOR\tA4, A5, A4\n\tMV\tA3, A5\n";
        }
        jump(done);
        defineLabel(big);
        out_ << "\tSUB\tA6, A0, A0\n";                           // count - 32: A0 is still 32 here
        if (left) out_ << "\tSHL\tA4, A0, A5\n\tZERO\tA4\n";
        else if (sign) out_ << "\tSHR\tA5, A0, A4\n\tSHR\tA5, 31, A5\n";
        else out_ << "\tSHRU\tA5, A0, A4\n\tZERO\tA5\n";
        defineLabel(done);
        return;
    }
    default: unsupported("this 64-bit operator");
    }
}

void Tms6747::visit(const Postfix &n) {
    const Type *t = n.type();
    genAddr(n.target());            // A4 = address
    push();                         // save address
    load(t);                        // A4 = old value
    pushValue(t);                   // save old value  (stack: top=old, next=addr)
    int step = n.step();
    if (t->isFloating()) {
        bool dp = isDouble(t);
        fpConst(t, static_cast<double>(step), "A6");   // the step, as a number
        out_ << (n.increment() ? "\tADD" : "\tSUB") << (dp ? "DP\tA5:A4, A7:A6, A5:A4" : "SP\tA4, A6, A4")
             << "\n\tNOP\t" << (dp ? 6 : 3) << "\n";
    } else if (isWide(t)) {
        if (step != 1) unsupported("a 64-bit step other than 1");
        if (n.increment()) out_ << "\tADD\tA4, 1, A4\n\tCMPEQ\t0, A4, A0\n\tADD\tA5, A0, A5\n";
        else               out_ << "\tCMPEQ\t0, A4, A0\n\tSUB\tA4, 1, A4\n\tSUB\tA5, A0, A5\n";
    } else {
        if (step >= 0 && step <= 31)
            out_ << (n.increment() ? "\tADD\tA4, " : "\tSUB\tA4, ") << step << ", A4\n";
        else { movImm("A0", step); out_ << (n.increment() ? "\tADD\tA4, A0, A4\n" : "\tSUB\tA4, A0, A4\n"); }
        narrowInt(t);               // A4 = new value
    }
    popValue(t, "A6");              // A6 = old value
    pop("A3");                      // A3 = address
    store(t, "A3");                 // *A3 = new value (A4)
    out_ << "\tMV\tA6, A4\n";        // the expression's value is the old value
    if (isWide(t)) out_ << "\tMV\tA7, A5\n";
}

void Tms6747::visit(const Return &n) {
    markLine(n);
    if (n.hasValue()) {
        n.value().accept(*this);    // result in A4
        if (n.value().type()->isStructOrUnion()) {
            // Copy it to where the caller asked (the pointer it passed in
            // A3, kept in the sret slot) and answer with that address.
            localAddr(sretSlot_, "A6");
            out_ << "\tLDW\t*A6, A6\n\tNOP\t4\n";
            copyBlock(n.value().type()->size(target_), "A4", "A6", n.value().type()->align(target_));
            out_ << "\tMV\tA6, A4\n";
        }
    }
    jump(returnLabel_);
}

// Conversions with a 64-bit integer on one side. Widening is a sign or zero
// fill of A5; narrowing drops it; to and from floating point is the EABI's.
void Tms6747::wideCast(const Type *from, const Type *to) {
    bool fromW = !from->isFloating() && isWide(from);
    bool toW = !to->isFloating() && isWide(to);
    const char *helper = nullptr;
    if (fromW && toW) return;
    if (toW && from->isFloating())
        helper = to->isSigned(target_) ? (isDouble(from) ? "__c6xabi_fixdlli" : "__c6xabi_fixflli")
                                       : (isDouble(from) ? "__c6xabi_fixdull" : "__c6xabi_fixfull");
    else if (fromW && to->isFloating())
        helper = from->isSigned(target_) ? (isDouble(to) ? "__c6xabi_fltllid" : "__c6xabi_fltllif")
                                         : (isDouble(to) ? "__c6xabi_fltulld" : "__c6xabi_fltullf");
    if (helper != nullptr) {
        spAdjust(-8);
        call(helper);
        spAdjust(8);
        return;
    }
    if (toW) out_ << (from->isSigned(target_) ? "\tSHR\tA4, 31, A5\n" : "\tZERO\tA5\n");
    else     narrowInt(to);
}

// A call under the C6000 EABI: ten word arguments in registers, the rest
// above the reserved word at *B15, B3 built by hand, B + NOP 5, result in A4.
// A variadic callee takes its last named argument onward on the stack.
void Tms6747::visit(const Call &n) {
    const std::vector<ExprPtr> &args = n.args();
    bool sret = n.type()->isStructOrUnion();

    // A struct argument is passed by the address of a copy: each is copied
    // into the slot the parser gave it first, and the slot's address then
    // stands in for the argument wherever it goes.
    for (std::size_t i = 0; i < args.size(); i++) {
        if (!args[i]->type()->isStructOrUnion()) continue;
        args[i]->accept(*this);               // A4 = the struct's address
        localAddr(n.argSlot(i), "A6");
        copyBlock(args[i]->type()->size(target_), "A4", "A6", args[i]->type()->align(target_));
    }

    std::size_t regCount = static_cast<std::size_t>(abi_.intCount);
    std::size_t inRegs = args.size() < regCount ? args.size() : regCount;
    if (n.isVariadic()) {
        std::size_t named = static_cast<std::size_t>(n.namedArgs());
        std::size_t last = named > 0 ? named - 1 : 0;   // the anchor for va_start
        if (last < inRegs) inRegs = last;
    }
    int onStack = static_cast<int>(args.size() - inRegs);

    // The area under the call: the reserved word, then the stack arguments,
    // a word each and a double 8-aligned. Opened even for a call with none,
    // so a value an enclosing expression pushed is not what sits at *B15.
    std::vector<int> at(onStack);
    int end = 4;
    for (int k = 0; k < onStack; k++) {
        const Type *t = args[inRegs + k]->type();
        if (isWide(t)) end = align8(end);
        at[k] = end;
        end += isWide(t) ? 8 : 4;
    }
    int area = align8(end);
    spAdjust(-area);
    for (int k = 0; k < onStack; k++) {
        const Type *t = args[inRegs + k]->type();
        genArg(n, inRegs + k);
        regAdd("B15", at[k], "B0");
        out_ << (isWide(t) ? "\tSTDW\tA5:A4, *B0\n" : "\tSTW\tA4, *B0\n");
    }

    if (n.callee() != nullptr) {
        n.callee()->accept(*this);  // the function's address
        push();
    }
    for (std::size_t i = 0; i + 1 < inRegs; i++) {
        genArg(n, i);
        pushValue(args[i]->type());
    }
    if (inRegs > 0) {
        // A double rides in the pair above its register: A5:A4, B5:B4, ...
        genArg(n, inRegs - 1);
        const char *last = abi_.intRegs[inRegs - 1];
        if (std::string(last) != "A4") moveValue(args[inRegs - 1]->type(), last);
        for (std::size_t i = inRegs - 1; i-- > 0; ) popValue(args[i]->type(), abi_.intRegs[i]);
    }
    if (inRegs > 6) usesSavedArgRegs_ = true;  // A10, B10, A12, B12 are callee-saved
    if (n.callee() != nullptr) pop("B1");
    if (sret) localAddr(n.resultSlot(), "A3");    // where the result goes

    call(n.callee() != nullptr ? "B1" : n.symbol());
    spAdjust(area);
    if (sret) localAddr(n.resultSlot(), "A4");    // the value: its address
}

// Where a stack-passed parameter (from firstStack_ on) sits relative to the
// caller's B15 - the layout the Call visit uses. i == ps.size() names the
// word past the last parameter, where a variadic function's extras begin.
int Tms6747::stackParamOffset(const std::vector<Param> &ps, std::size_t i) {
    int end = 4;
    for (std::size_t k = firstStack_; k <= i; k++) {
        if (k < ps.size() && isWide(ps[k].type)) end = align8(end);
        if (k == i) return end;
        end += isWide(ps[k].type) ? 8 : 4;
    }
    return end;
}

// Argument i into A4: its value, or for a struct the address of its copy.
void Tms6747::genArg(const Call &n, std::size_t i) {
    if (n.args()[i]->type()->isStructOrUnion()) localAddr(n.argSlot(i), "A4");
    else n.args()[i]->accept(*this);
}

// The call itself: the return address into B3, the branch - to a symbol or
// through B1 - and its five delay slots. The caller has opened the area.
void Tms6747::call(const std::string &target) {
    std::string ret = label("ret", nextLabel());
    movSym("B3", ret);
    out_ << "\tB\t" << target << "\n\tNOP\t5\n";
    defineLabel(ret);
    hasCall_ = true;
}

// Between integers and pointers a conversion is a narrowing at most: A4
// holds every value extended to the word, so widening is nothing and an
// array decays to the address it already is.
void Tms6747::visit(const Cast &n) {
    n.value().accept(*this);
    const Type *from = n.value().type(), *to = n.type();
    if (to->isVoid()) return;
    if (from->isArray() || from->isFunction()) return;   // decay: the address it is
    bool fromF = from->isFloating(), toF = to->isFloating();
    bool fromW = !fromF && isWide(from), toW = !toF && isWide(to);
    if (fromW || toW) { wideCast(from, to); return; }
    if (fromF && toF) {
        // float <-> double; long double is double here.
        bool fromD = isDouble(from), toD = isDouble(to);
        if (fromD && !toD) out_ << "\tDPSP\tA5:A4, A4\n\tNOP\t1\n";
        if (!fromD && toD) out_ << "\tSPDP\tA4, A5:A4\n\tNOP\t1\n";
        return;
    }
    if (!fromF && toF) {
        // Integer to floating: INTSP/INTDP, or their U forms for unsigned.
        const char *u = from->isSigned(target_) ? "" : "U";
        if (isDouble(to)) out_ << "\tINTDP" << u << "\tA4, A5:A4\n\tNOP\t4\n";
        else              out_ << "\tINTSP" << u << "\tA4, A4\n\tNOP\t3\n";
        return;
    }
    if (fromF && !toF) {
        // Floating to integer, truncating toward zero as C says. SPTRUNC and
        // DPTRUNC give a signed word; a full unsigned word is the helper's.
        bool dp = isDouble(from);
        if (to->size(target_) == 4 && !to->isSigned(target_)) {
            spAdjust(-8);
            call(dp ? "__c6xabi_fixdu" : "__c6xabi_fixfu");
            spAdjust(8);
        } else {
            out_ << (dp ? "\tDPTRUNC\tA5:A4, A4" : "\tSPTRUNC\tA4, A4") << "\n\tNOP\t3\n";
        }
        narrowInt(to);
        return;
    }
    narrowInt(to);
}
void Tms6747::visit(const StrLit &n) { genAddr(n); }  // an array: its address
// va_list is a char *: va_start points it at the word past the last named
// parameter, on the stack with everything after it; va_arg reads the value
// there (8-byte ones 8-aligned, a struct through its pointer) and steps on.
void Tms6747::visit(const VaStart &n) {
    n.list().accept(*this);                 // A4 = &ap
    regAdd("A15", linkBytes_ + vaStart_, "A6");
    out_ << "\tSTW\tA6, *A4\n";
}
void Tms6747::visit(const VaArg &n) {
    const Type *t = n.type();
    bool byRef = t->isStructOrUnion();
    int slot = isWide(t) ? 8 : 4;
    n.list().accept(*this);                 // A4 = &ap
    out_ << "\tLDW\t*A4, A6\n\tNOP\t4\n";  // A6 = ap
    if (slot == 8) out_ << "\tADD\tA6, 7, A6\n\tAND\tA6, -8, A6\n";
    out_ << "\tADD\tA6, " << slot << ", A3\n\tSTW\tA3, *A4\n";   // ap += slot
    out_ << "\tMV\tA6, A4\n";
    if (byRef) { out_ << "\tLDW\t*A4, A4\n\tNOP\t4\n"; return; }  // the struct's address
    load(t);
}
void Tms6747::visit(const Switch &n) {
    if (isWide(n.cond().type())) unsupported("a switch on a 64-bit value");
    Walker::visit(n);
}

void Tms6747::visit(const MemberAccess &n) {
    if (n.isBitField()) {
        bitFieldUnitAddr(n);
        load(n.type());
        bitFieldExtract(n);
        return;
    }
    genAddr(n);
    load(n.type());
}

// ---- functions and the file ----------------------------------------------
// Initialised data, in TI's directives: .byte, .short and .word (32 bits;
// TI's .long is 32 bits too, so a 64-bit piece is two words, low first), a
// .word of a symbol with its addend for a relocated piece, .space for a gap.
void Tms6747::emitGlobal(const Global &g, Segment seg) {
    int size = g.type->size(target_);
    // The type's own alignment: lifting a 16-byte object to 16 is x86's rule.
    int align = g.align > g.type->align(target_) ? g.align : g.type->align(target_);
    // An inline object - a vtable, a typeinfo, a template's static member -
    // is one object however many units define it: `.weak` in place of
    // `.global`, and zero-filled ones in .data by .space, not the .bss form.
    if (g.isInline)       out_ << "\t.weak\t" << g.symbol << "\n";
    else if (!g.isStatic) out_ << "\t.global " << g.symbol << "\n";

    if (seg == Segment::Bss && !g.isInline) {
        out_ << "\t.bss\t" << g.symbol << ", " << size << ", " << align << "\n";
        return;
    }
    if (seg == Segment::Bss) out_ << "\t.data\n";

    if (align > 1) out_ << "\t.align\t" << align << "\n";
    // The word in front, where there is one: after the alignment, so that it
    // is the label and not the block that ends up aligned. A pointer here is
    // a word.
    if (!g.prefixWord.empty()) out_ << "\t.word\t" << symName(g.prefixWord) << "\n";
    out_ << g.symbol << ":\n";
    if (seg == Segment::Bss) { out_ << "\t.space\t" << size << "\n"; return; }
    int at = 0;
    for (const GlobalPiece &p : g.init) {
        if (p.offset > at) out_ << "\t.space\t" << (p.offset - at) << "\n";
        if (!p.symbol.empty()) {
            if (p.size != 4) unsupported("a relocated piece that is not a word");
            out_ << "\t.word\t" << symName(p.symbol);
            if (p.value > 0) out_ << "+" << p.value;
            else if (p.value < 0) out_ << "-" << -p.value;
            out_ << "\n";
        } else {
            switch (p.size) {
            case 1: out_ << "\t.byte\t" << p.value << "\n"; break;
            case 2: out_ << "\t.short\t" << p.value << "\n"; break;
            case 4: out_ << "\t.word\t" << p.value << "\n"; break;
            case 8: {
                unsigned long long v = static_cast<unsigned long long>(p.value);
                out_ << "\t.word\t" << (v & 0xffffffffu) << "\n";
                out_ << "\t.word\t" << (v >> 32) << "\n";
                break;
            }
            default: unsupported("a data piece of this size");
            }
        }
        at = p.offset + p.size;
    }
    if (at < size) out_ << "\t.space\t" << (size - at) << "\n";
}

// String literals into .const as plain .byte lists - no escape syntax to
// get wrong, and wide strings are the same form - then the globals by
// segment: constants in .const, initialised data in .data, the rest .bss.
void Tms6747::emitData(const Program &program) {
    bool inConst = !program.strings.empty();
    if (inConst) out_ << "\t.sect\t\".const\"\n";
    for (const StringLit &s : program.strings) {
        if (s.width > 1) out_ << "\t.align\t" << s.width << "\n";
        out_ << symName(s.label) << ":\n";
        for (std::size_t i = 0; i < s.bytes.size(); i++) {
            if (i % 16 == 0) out_ << "\t.byte\t";
            out_ << static_cast<int>(static_cast<unsigned char>(s.bytes[i]));
            out_ << ((i + 1 == s.bytes.size() || i % 16 == 15) ? "\n" : ", ");
        }
    }

    struct Bucket { Segment seg; const char *open; };
    const Bucket order[] = {
        { Segment::Const,          "\t.sect\t\".const\"\n" },
        { Segment::ConstRelocated, "\t.sect\t\".const\"\n" },
        { Segment::Data,           "\t.data\n" },
        { Segment::Bss,            nullptr },
    };
    for (const Bucket &b : order) {
        bool opened = inConst && b.seg != Segment::Data;   // .const is open already
        for (const Global &g : program.globals) {
            if (segmentFor(g) != b.seg) continue;
            if (!opened && b.open != nullptr) out_ << b.open;
            opened = true;
            emitGlobal(g, b.seg);
        }
    }
}

// Parameters arrive in the argument registers and, from the eleventh, on
// the stack; each is copied to its own frame slot so the body sees a local.
// Runs after the body has been walked, when the frame link's size is known.
void Tms6747::emitParams(const Function &fn) {
    const std::vector<Param> &ps = fn.params();
    for (std::size_t i = 0; i < ps.size(); i++) {
        const Type *t = ps[i].type;
        bool byRef = t->isStructOrUnion();      // the address of the caller's copy
        if (i < firstStack_) {
            const char *reg = abi_.intRegs[i];
            if (std::string(reg) != "A4") {
                out_ << "\tMV\t" << reg << ", A4\n";
                if (isWide(t)) {
                    std::string hi = pairOf(reg).substr(0, pairOf(reg).find(':'));
                    out_ << "\tMV\t" << hi << ", A5\n";
                }
            }
        } else {
            // The caller's B15 was A15 + the link; its stack arguments start
            // one word above that.
            regAdd("A15", linkBytes_ + stackParamOffset(ps, i), "A0");
            out_ << (isWide(t) ? "\tLDDW\t*A0, A5:A4\n\tNOP\t4\n" : "\tLDW\t*A0, A4\n\tNOP\t4\n");
        }
        if (byRef) {
            // Through A1, not A6: A6 is the third argument's register, still
            // to be read when an earlier struct parameter is being copied.
            localAddr(ps[i].offset, "A1");
            copyBlock(t->size(target_), "A4", "A1", t->align(target_));
            continue;
        }
        localAddr(ps[i].offset, "A0");
        store(t, "A0");
    }
}

// The frame link A15 points at: the caller's A15, B3, then these four when
// the body loads them for a call. Locals lie below it at A15 - offset.

static const char *const kSavedArgRegs[] = { "A10", "B10", "A12", "B12" };

void Tms6747::emitFunction(const Function &fn) {
    resetLabels();
    // The symbol, not the name: with C++ linkage that is the mangled name, and
    // two overloads share a name but must not share a label.
    functionName_ = fn.symbol();
    labelPrefix_ = "L." + fn.symbol() + ".";
    returnLabel_ = "L.return." + fn.symbol();
    hasCall_ = false;
    clearCallSites();
    usesSavedArgRegs_ = false;
    linkBytes_ = 8;

    sretSlot_ = fn.sretSlot();

    // Which parameters the caller put on the stack, and where a variadic
    // function's unnamed arguments start. A variadic function keeps the full
    // link so that va_start, emitted inside the body, knows its size.
    std::size_t regCount = static_cast<std::size_t>(abi_.intCount);
    firstStack_ = regCount;
    vaStart_ = 0;
    if (fn.isVariadic()) {
        std::size_t named = fn.params().size();
        std::size_t last = named > 0 ? named - 1 : 0;
        if (last < firstStack_) firstStack_ = last;
        usesSavedArgRegs_ = true;
        linkBytes_ = 24;
        vaStart_ = stackParamOffset(fn.params(), named);
    }

    // The body first, into its own text: what it does decides the prologue.
    fn.body().accept(*this);
    // Falling off the end returns 0 - main's C99 meaning, and what the other
    // backends do for every function - or the result pointer for a struct.
    if (sretSlot_ != 0) { localAddr(sretSlot_, "A4"); out_ << "\tLDW\t*A4, A4\n\tNOP\t4\n"; }
    else if (isWide(fn.returns())) out_ << "\tZERO\tA5:A4\n";
    else out_ << "\tZERO\tA4\n";
    std::string body = out_.str();
    out_.str(std::string());
    if (usesSavedArgRegs_) linkBytes_ = 24;
    if (sretSlot_ != 0) {
        // The caller's pointer to where the result goes, from A3, kept in
        // its slot for the return to find - before the parameters are copied
        // in, since a struct parameter's copy goes through A3.
        localAddr(sretSlot_, "A0");
        out_ << "\tSTW\tA3, *A0\n";
    }
    emitParams(fn);
    std::string params = out_.str();
    out_.str(std::string());

    out_ << "\t.text\n";
    // An inline definition may appear in several translation units, so the
    // linker keeps one rather than rejecting the rest: a weak definition,
    // `.weak` in place of `.global` for TI's assembler.
    if (fn.isInline())       out_ << "\t.weak\t" << fn.symbol() << "\n";
    else if (!fn.isStatic()) out_ << "\t.global " << fn.symbol() << "\n";
    out_ << fn.symbol() << ":\n";
    // A second name for the same code (Function::alias): a label at the same
    // address, not a copy of the body.
    if (!fn.alias().empty()) {
        if (fn.isInline())       out_ << "\t.weak\t" << fn.alias() << "\n";
        else if (!fn.isStatic()) out_ << "\t.global " << fn.alias() << "\n";
        out_ << fn.alias() << ":\n";
    }

    int frame = align8(fn.frameSize());
    bool needFrame = frame > 0 || hasCall_ || !fn.params().empty() || sretSlot_ != 0;
    if (needFrame) {
        spAdjust(-linkBytes_);
        out_ << "\tSTW\tA15, *B15\n";
        if (hasCall_) {                         // a leaf leaves B3 alone
            regAdd("B15", 4, "B0");
            out_ << "\tSTW\tB3, *B0\n";
        }
        if (usesSavedArgRegs_)
            for (int k = 0; k < 4; k++) {
                regAdd("B15", 8 + 4 * k, "B0");
                out_ << "\tSTW\t" << kSavedArgRegs[k] << ", *B0\n";
            }
        out_ << "\tMV\tB15, A15\n";
        spAdjust(-frame);
    }

    out_ << params << body;

    out_ << returnLabel_ << ":\n";
    if (needFrame) {
        out_ << "\tMV\tA15, B15\n";                 // drop the locals: SP = FP
        if (hasCall_) {
            regAdd("B15", 4, "B0");
            out_ << "\tLDW\t*B0, B3\n";
        }
        if (usesSavedArgRegs_)
            for (int k = 0; k < 4; k++) {
                regAdd("B15", 8 + 4 * k, "B0");
                out_ << "\tLDW\t*B0, " << kSavedArgRegs[k] << "\n";
            }
        out_ << "\tLDW\t*B15, A15\n\tNOP\t4\n";      // the caller's FP, last
        spAdjust(linkBytes_);                          // pop the link
    }
    out_ << "\tB\tB3\n\tNOP\t5\n";
    emitExceptionTable(fn, frame);

    file_ += out_.str();
    out_.str(std::string());
}

void Tms6747::run(const Program &program) {
    emitData(program);
    file_ += out_.str();
    out_.str(std::string());
    for (const Function &fn : program.functions) emitFunction(fn);
    // Dynamic initialisation: the init function's address in .init_array,
    // which the EABI's start-up code - and the VM6747 emulator - walk before
    // main.
    if (!program.initFunction.empty()) {
        out_ << "\t.sect\t\".init_array\"\n\t.align\t4\n\t.word\t" << program.initFunction << "\n";
        file_ += out_.str();
        out_.str(std::string());
    }
    sink_ << file_;
}
