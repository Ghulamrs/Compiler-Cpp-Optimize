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
    // C6000 EABI argument registers, for the calls milestone; unused so far.
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
        unsupported("the address of a global");
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

void Tms6747::visit(const Call &) { unsupported("a function call"); }
void Tms6747::visit(const Cast &) { unsupported("a cast"); }
void Tms6747::visit(const StrLit &) { unsupported("a string literal"); }
void Tms6747::visit(const VaStart &) { unsupported("va_start"); }
void Tms6747::visit(const VaArg &) { unsupported("va_arg"); }
void Tms6747::visit(const MemberAccess &) { unsupported("a member access"); }

// ---- functions and the file ----------------------------------------------
void Tms6747::emitData(const Program &program) {
    if (!program.globals.empty()) unsupported("a global variable");
    if (!program.strings.empty()) unsupported("a string literal");
}

void Tms6747::emitFunction(const Function &fn) {
    resetLabels();
    functionName_ = fn.name();
    labelPrefix_ = "L." + fn.name() + ".";
    returnLabel_ = "L.return." + fn.name();

    out_ << "\t.text\n";
    if (!fn.isStatic()) out_ << "\t.global " << fn.name() << "\n";
    out_ << fn.name() << ":\n";

    if (fn.sretSlot() != 0 || fn.isVariadic() || !fn.params().empty())
        unsupported("a function with parameters, a struct return, or varargs");

    int frame = align8(fn.frameSize());
    bool needFrame = frame > 0;
    if (needFrame) {
        // Save the caller's frame pointer (A15 is callee-saved), point A15 at
        // our frame, then open the locals below it. B15 is the stack pointer.
        out_ << "\tSUB\tB15, 8, B15\n";
        out_ << "\tSTW\tA15, *B15\n";
        out_ << "\tMV\tB15, A15\n";
        spAdjust(-frame);
    }

    fn.body().accept(*this);

    out_ << returnLabel_ << ":\n";
    if (needFrame) {
        out_ << "\tMV\tA15, B15\n";                 // drop the locals: SP = FP
        out_ << "\tLDW\t*B15, A15\n\tNOP\t4\n";      // restore the caller's FP
        out_ << "\tADD\tB15, 8, B15\n";             // pop the saved-FP slot
    }
    out_ << "\tB\tB3\n\tNOP\t5\n";
}

void Tms6747::run(const Program &program) {
    emitData(program);
    for (const Function &fn : program.functions) emitFunction(fn);
    sink_ << out_.str();
}
