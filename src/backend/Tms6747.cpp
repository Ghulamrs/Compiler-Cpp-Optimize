#include "Tms6747.h"

#include "../Abi.h"
#include "../Ast.h"

#include <cstdio>
#include <cstdlib>
#include <ostream>
#include <string>

static int align8(int n) { return (n + 7) / 8 * 8; }

// ---- the target: sizes for a 32-bit C6000 (EABI) -------------------------
int Tms6747Target::sizeOf(Kind k) const {
    switch (k) {
    case Kind::Void:                                     return 1;
    case Kind::Char: case Kind::SChar: case Kind::UChar: return 1;
    case Kind::Short: case Kind::UShort:                 return 2;
    case Kind::Int: case Kind::UInt:                     return 4;
    case Kind::Long: case Kind::ULong:                   return 4;
    case Kind::LongLong: case Kind::ULongLong:           return 8;
    case Kind::Float:                                    return 4;
    case Kind::Double: case Kind::LongDouble:            return 8;
    case Kind::Pointer:                                  return 4;
    default:
        std::fprintf(stderr, "target: no size for this type yet (tms6747)\n");
        std::exit(1);
    }
}
int Tms6747Target::alignOf(Kind k) const { return sizeOf(k); }

// ---- the backend ---------------------------------------------------------
const Abi &Tms6747Backend::abi() const {
    // C6000 EABI argument registers, in order; the result comes back in A4.
    static const char *const kIntRegs[] = {
        "A4", "B4", "A6", "B6", "A8", "B8", "A10", "B10", "A12", "B12"
    };
    static const Abi kAbi = {
        kIntRegs, 10, nullptr, 0, true, 0, 8, true, false, "A0", "A0", false, true
    };
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

void Tms6747::push() {
    out_ << "\tSUB\tB15, 8, B15\n";
    out_ << "\tSTW\tA4, *B15\n";
}
void Tms6747::pop(const char *reg) {
    out_ << "\tLDW\t*B15, " << reg << "\n\tNOP\t4\n";
    out_ << "\tADD\tB15, 8, B15\n";
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
    unsupported("this address");
}

void Tms6747::load(const Type *t) {
    if (t->isArray() || t->isStructOrUnion()) return;   // the address is the value
    if (t->isFloating()) unsupported("a floating-point load");
    int sz = t->size(target_);
    if (sz > 4) unsupported("a 64-bit load");
    bool sign = t->isSigned(target_);
    const char *op = sz == 1 ? (sign ? "LDB" : "LDBU")
                   : sz == 2 ? (sign ? "LDH" : "LDHU")
                             : "LDW";
    out_ << "\t" << op << "\t*A4, A4\n\tNOP\t4\n";
}

void Tms6747::store(const Type *t, const char *addrReg) {
    if (t->isFloating()) unsupported("a floating-point store");
    int sz = t->size(target_);
    if (sz > 4) unsupported("a 64-bit store");
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
void Tms6747::landingPad(int, int) { unsupported("a C++ landing pad"); }
void Tms6747::genTruth(const Expr &e) {
    e.accept(*this);
    if (e.type()->isFloating()) unsupported("a floating-point truth test");
    // A4 = (A4 != 0) ? 1 : 0
    out_ << "\tCMPEQ\t0, A4, A4\n";
    out_ << "\tXOR\t1, A4, A4\n";
}

// ---- expressions ---------------------------------------------------------
void Tms6747::visit(const Num &n) {
    if (n.type()->isFloating()) unsupported("a floating-point constant");
    movImm("A4", n.value());
}
void Tms6747::visit(const Var &n) { genAddr(n); load(n.type()); }

void Tms6747::visit(const Assign &n) {
    if (n.type()->isStructOrUnion()) unsupported("a struct assignment");
    n.value().accept(*this);        // A4 = value
    push();                         // save the value
    genAddr(n.target());            // A4 = address
    out_ << "\tMV\tA4, A6\n";        // A6 = address
    pop("A4");                       // A4 = value again
    store(n.type(), "A6");           // *A6 = A4 ; A4 stays the value (the result)
}

void Tms6747::visit(const Unary &n) {
    switch (n.op()) {
    case '+': n.operand().accept(*this); return;
    case '-':
        n.operand().accept(*this);
        if (n.type()->isFloating()) unsupported("floating-point negation");
        out_ << "\tNEG\tA4, A4\n";
        return;
    case '~':
        n.operand().accept(*this);
        out_ << "\tNOT\tA4, A4\n";
        return;
    case '!':
        n.operand().accept(*this);
        if (n.operand().type()->isFloating()) unsupported("a floating-point '!'");
        out_ << "\tCMPEQ\t0, A4, A4\n";
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

    if (n.lhs().type()->isFloating() || n.rhs().type()->isFloating())
        unsupported("a floating-point operator");

    n.lhs().accept(*this);          // A4 = lhs
    push();
    n.rhs().accept(*this);          // A4 = rhs
    out_ << "\tMV\tA4, A6\n";        // A6 = rhs
    pop("A4");                       // A4 = lhs   (now A4=lhs, A6=rhs)

    bool sign = n.lhs().type()->isSigned(target_);
    switch (n.op()) {
    case BinOp::Add:    out_ << "\tADD\tA4, A6, A4\n";  narrowInt(n.type()); return;
    case BinOp::Sub:    out_ << "\tSUB\tA4, A6, A4\n";  narrowInt(n.type()); return;
    case BinOp::Mul:    out_ << "\tMPY32\tA4, A6, A4\n"; narrowInt(n.type()); return;
    case BinOp::BitAnd: out_ << "\tAND\tA4, A6, A4\n";  narrowInt(n.type()); return;
    case BinOp::BitOr:  out_ << "\tOR\tA4, A6, A4\n";   narrowInt(n.type()); return;
    case BinOp::BitXor: out_ << "\tXOR\tA4, A6, A4\n";  narrowInt(n.type()); return;
    case BinOp::Shl:    out_ << "\tSHL\tA4, A6, A4\n";  narrowInt(n.type()); return;
    case BinOp::Shr:
        out_ << (sign ? "\tSHR\tA4, A6, A4\n" : "\tSHRU\tA4, A6, A4\n");
        narrowInt(n.type());
        return;
    case BinOp::Div: case BinOp::Mod:
        unsupported("integer division (the C6000 has no divide instruction)");
        return;
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

void Tms6747::visit(const Postfix &n) {
    if (n.type()->isFloating()) unsupported("a floating-point ++/--");
    genAddr(n.target());            // A4 = address
    push();                         // save address
    load(n.type());                 // A4 = old value
    push();                         // save old value  (stack: top=old, next=addr)
    int step = n.step();
    if (step >= 0 && step <= 31)
        out_ << (n.increment() ? "\tADD\tA4, " : "\tSUB\tA4, ") << step << ", A4\n";
    else { movImm("A0", step); out_ << (n.increment() ? "\tADD\tA4, A0, A4\n" : "\tSUB\tA4, A0, A4\n"); }
    narrowInt(n.type());            // A4 = new value
    pop("A6");                      // A6 = old value
    pop("A3");                      // A3 = address
    store(n.type(), "A3");          // *A3 = new value (A4)
    out_ << "\tMV\tA6, A4\n";        // the expression's value is the old value
}

void Tms6747::visit(const Return &n) {
    markLine(n);
    if (n.hasValue()) {
        if (n.value().type()->isStructOrUnion()) unsupported("returning a struct");
        n.value().accept(*this);    // result in A4
    }
    jump(returnLabel_);
}

// A call under the C6000 EABI. Arguments are evaluated left to right onto the
// expression stack and popped into A4, B4, A6, B6, A8, B8, A10, B10, A12, B12
// (the last one straight from A4); the eleventh onward are stored above the
// reserved word at *B15 in an area opened for the call. The return address is
// built into B3 by hand and the branch takes its five delay slots as NOPs, a
// form every C6000 accepts. The result is left in A4.
//
// A variadic callee is the exception: its last named argument and everything
// after it go on the stack, in order, so that va_start can step from the
// named one to the rest - the C6000 convention, and the reason `printf("%d",
// x)` puts both the format and x on the stack. The ones before it ride in
// registers as usual.
void Tms6747::visit(const Call &n) {
    const std::vector<ExprPtr> &args = n.args();
    if (n.type()->isStructOrUnion()) unsupported("a call returning a struct");
    if (n.type()->isFloating()) unsupported("a call returning a floating-point value");
    if (n.type()->size(target_) > 4) unsupported("a call returning a 64-bit value");
    for (const ExprPtr &a : args) {
        if (a->type()->isStructOrUnion()) unsupported("a struct argument");
        if (a->type()->isFloating()) unsupported("a floating-point argument");
        if (a->type()->size(target_) > 4) unsupported("a 64-bit argument");
    }

    std::size_t regCount = static_cast<std::size_t>(abi_.intCount);
    std::size_t inRegs = args.size() < regCount ? args.size() : regCount;
    if (n.isVariadic()) {
        std::size_t named = static_cast<std::size_t>(n.namedArgs());
        std::size_t last = named > 0 ? named - 1 : 0;   // the anchor for va_start
        if (last < inRegs) inRegs = last;
    }
    int onStack = static_cast<int>(args.size() - inRegs);

    // The area under the call: the reserved word, then the stack arguments.
    // Opened even for a call with none, so that a value an enclosing
    // expression has pushed is not what sits at *B15 during the call.
    int area = align8(4 + 4 * onStack);
    spAdjust(-area);
    for (int k = 0; k < onStack; k++) {
        args[inRegs + k]->accept(*this);
        regAdd("B15", 4 + 4 * k, "B0");
        out_ << "\tSTW\tA4, *B0\n";
    }

    if (n.callee() != nullptr) {
        n.callee()->accept(*this);  // the function's address
        push();
    }
    for (std::size_t i = 0; i + 1 < inRegs; i++) {
        args[i]->accept(*this);
        push();
    }
    if (inRegs > 0) {
        args[inRegs - 1]->accept(*this);
        const char *last = abi_.intRegs[inRegs - 1];
        if (std::string(last) != "A4") out_ << "\tMV\tA4, " << last << "\n";
        for (std::size_t i = inRegs - 1; i-- > 0; ) pop(abi_.intRegs[i]);
    }
    if (inRegs > 6) usesSavedArgRegs_ = true;  // A10, B10, A12, B12 are callee-saved
    if (n.callee() != nullptr) pop("B1");

    std::string ret = label("ret", nextLabel());
    movSym("B3", ret);
    if (n.callee() != nullptr) out_ << "\tB\tB1\n";
    else                       out_ << "\tB\t" << n.symbol() << "\n";
    out_ << "\tNOP\t5\n";
    defineLabel(ret);
    spAdjust(area);
    hasCall_ = true;
}

// A conversion between integers and pointers is a narrowing at most: A4 holds
// every value sign- or zero-extended to the word, so widening is nothing and
// an array decays to the address it already is. Floating-point and 64-bit
// conversions wait for their types.
void Tms6747::visit(const Cast &n) {
    n.value().accept(*this);
    const Type *from = n.value().type(), *to = n.type();
    if (to->isVoid()) return;
    if (from->isArray() || from->isFunction()) return;   // decay: the address it is
    if (from->isFloating() || to->isFloating()) unsupported("a floating-point conversion");
    if (from->size(target_) > 4 || to->size(target_) > 4) unsupported("a 64-bit conversion");
    narrowInt(to);
}
void Tms6747::visit(const StrLit &n) { genAddr(n); }  // an array: its address
void Tms6747::visit(const VaStart &) { unsupported("va_start"); }
void Tms6747::visit(const VaArg &) { unsupported("va_arg"); }
void Tms6747::visit(const MemberAccess &) { unsupported("a member access"); }

// ---- functions and the file ----------------------------------------------
// Initialised data, in TI's directives: .byte, .short and .word (32 bits;
// TI's .long is 32 bits too, so a 64-bit piece is two words, low first), a
// .word of a symbol with its addend for a relocated piece, .space for a gap.
void Tms6747::emitGlobal(const Global &g, Segment seg) {
    int size = g.type->size(target_);
    // The type's own alignment: the x86/arm64 rule that lifts a 16-byte object
    // to 16 is those ABIs', not TI's.
    int align = g.type->align(target_);
    // The symbol is the linker's name (Itanium leaves a variable's alone). An
    // inline object - a vtable, a typeinfo - wants a weak symbol the linker
    // folds, and the prefix word is an 8-byte pointer laid before a label;
    // neither has a TI spelling chosen yet, so both are refused, not guessed.
    if (g.isInline) unsupported("an inline object (a weak symbol)");
    if (!g.prefixWord.empty()) unsupported("an object with a prefix word");
    if (!g.isStatic) out_ << "\t.global " << g.symbol << "\n";

    if (seg == Segment::Bss) {
        out_ << "\t.bss\t" << g.symbol << ", " << size << ", " << align << "\n";
        return;
    }

    if (align > 1) out_ << "\t.align\t" << align << "\n";
    out_ << g.symbol << ":\n";
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

// String literals into .const as plain .byte lists - no escape syntax to get
// wrong, and the same form serves wide strings, whose bytes the parser has
// already laid out - then the globals by segment: constants (relocated or
// not) in .const, initialised data in .data, and the rest as .bss.
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

// Parameters arrive in the argument registers and, from the eleventh, on the
// stack above the caller's reserved word; each is copied to its own frame
// slot, so the body sees every parameter as a local. Runs after the body has
// been walked, when the size of the frame link is known.
void Tms6747::emitParams(const Function &fn) {
    const std::vector<Param> &ps = fn.params();
    std::size_t regCount = static_cast<std::size_t>(abi_.intCount);
    for (std::size_t i = 0; i < ps.size(); i++) {
        const Type *t = ps[i].type;
        if (t->isStructOrUnion()) unsupported("a struct parameter");
        if (t->isFloating()) unsupported("a floating-point parameter");
        if (t->size(target_) > 4) unsupported("a 64-bit parameter");
        if (i < regCount) {
            const char *reg = abi_.intRegs[i];
            if (std::string(reg) != "A4") out_ << "\tMV\t" << reg << ", A4\n";
        } else {
            // The caller's B15 was A15 + the link; its stack arguments start
            // one word above that.
            int k = static_cast<int>(i - regCount);
            regAdd("A15", linkBytes_ + 4 + 4 * k, "A0");
            out_ << "\tLDW\t*A0, A4\n\tNOP\t4\n";
        }
        localAddr(ps[i].offset, "A0");
        store(t, "A0");
    }
}

// The frame link, saved below the caller's stack and pointed at by A15:
//   *B15+0  the caller's A15        *B15+4  the return address, B3
//   *B15+8  A10   +12 B10   +16 A12   +20 B12   (only when the body loads them)
// Locals lie below the link at A15 - offset. A leaf with no locals and no
// parameters keeps no link at all.
static const char *const kSavedArgRegs[] = { "A10", "B10", "A12", "B12" };

void Tms6747::emitFunction(const Function &fn) {
    resetLabels();
    // The symbol, not the name: with C++ linkage that is the mangled name, and
    // two overloads share a name but must not share a label.
    functionName_ = fn.symbol();
    labelPrefix_ = "L." + fn.symbol() + ".";
    returnLabel_ = "L.return." + fn.symbol();
    hasCall_ = false;
    usesSavedArgRegs_ = false;
    linkBytes_ = 8;

    if (fn.sretSlot() != 0) unsupported("a function returning a struct");
    if (fn.isVariadic()) unsupported("a variadic function");

    // The body goes first, into its own text, because what it does decides the
    // prologue: a call means B3 must be saved, and a call with more than six
    // arguments means A10/B10/A12/B12 must be too.
    fn.body().accept(*this);
    std::string body = out_.str();
    out_.str(std::string());
    if (usesSavedArgRegs_) linkBytes_ = 24;
    emitParams(fn);
    std::string params = out_.str();
    out_.str(std::string());

    out_ << "\t.text\n";
    // An inline definition may appear in several translation units and needs
    // a weak symbol the linker folds; that spelling is not settled for the TI
    // tools yet, so the shape is refused rather than emitted strong.
    if (fn.isInline()) unsupported("an inline function (a weak symbol)");
    if (!fn.isStatic()) out_ << "\t.global " << fn.symbol() << "\n";
    out_ << fn.symbol() << ":\n";
    // A second name for the same code (Function::alias): a label at the same
    // address, not a copy of the body.
    if (!fn.alias().empty()) {
        if (!fn.isStatic()) out_ << "\t.global " << fn.alias() << "\n";
        out_ << fn.alias() << ":\n";
    }

    int frame = align8(fn.frameSize());
    bool needFrame = frame > 0 || hasCall_ || !fn.params().empty();
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

    file_ += out_.str();
    out_.str(std::string());
}

void Tms6747::run(const Program &program) {
    emitData(program);
    file_ += out_.str();
    out_.str(std::string());
    for (const Function &fn : program.functions) emitFunction(fn);
    sink_ << file_;
}
