#include "Tms6747.h"

#include "../Abi.h"
#include "../Ast.h"

#include <cstdio>
#include <cstdlib>
#include <ostream>
#include <string>

// ---- the target: sizes for a 32-bit C6000 (EABI) -------------------------
int Tms6747Target::sizeOf(Kind k) const {
    switch (k) {
    case Kind::Void:                                     return 1;
    case Kind::Char: case Kind::SChar: case Kind::UChar: return 1;
    case Kind::Short: case Kind::UShort:                 return 2;
    case Kind::Int: case Kind::UInt:                     return 4;
    // C6000 EABI: long and pointers are 32-bit; long long is 64-bit.
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

// ---- the backend: ABI, identity, and the code generator ------------------
static Abi tms6747Eabi() {
    // C6000 EABI: integer and pointer arguments in A4, B4, A6, B6, A8, B8,
    // A10, B10, A12, B12; result in A4 (A5:A4 for 64-bit). On C674x floating
    // arguments share those registers, so there is no separate floating file.
    // Not exercised until the calls milestone; described, not guessed.
    static const char *const kIntRegs[] = {
        "A4", "B4", "A6", "B6", "A8", "B8", "A10", "B10", "A12", "B12"
    };
    Abi a;
    a.intRegs = kIntRegs;   a.intCount = 10;
    a.positional = true;
    a.structReturnLimit = 8;          // placeholder until the calls milestone
    a.scratch = "A0";       a.scratch32 = "A0";
    a.elfSymbolAttributes = true;     // EABI is ELF
    return a;
}

const Abi &Tms6747Backend::abi() const {
    static const Abi kAbi = tms6747Eabi();
    return kAbi;
}

const char *const *Tms6747Backend::identityMacros() const {
    static const char *const kMacros[] = {
        "__tms320c6x__=1",
        "__TMS320C6X__=1",
        "__TMS320C6700__=1",    // C674x is the C67x+ floating-point line
        "__TMS320C6740__=1",
        "__C6X__=1",
        "__LITTLE_ENDIAN__=1",
        nullptr
    };
    return kMacros;
}

std::unique_ptr<CodeGen> Tms6747Backend::codegen(std::ostream &sink, Syntax) const {
    return std::unique_ptr<CodeGen>(new Tms6747(sink, target_, abi()));
}

// ---- the code generator --------------------------------------------------
void Tms6747::unsupported(const char *what) {
    std::fprintf(stderr, "codegen: %s is not supported yet by the tms6747 backend\n",
                 what);
    std::exit(1);
}

// A 32-bit constant in two halves: MVKL takes the low 16 (sign extended), MVKH
// sets the high 16. Given the whole constant, the assembler takes the right
// half from each.
void Tms6747::movImm(const char *reg, long long value) {
    long long v = static_cast<int>(value);
    out_ << "\tMVKL\t" << v << ", " << reg << "\n";
    out_ << "\tMVKH\t" << v << ", " << reg << "\n";
}

std::string Tms6747::label(const char *kind, int id) const {
    return labelPrefix_ + kind + std::to_string(id);
}
std::string Tms6747::userLabel(const std::string &name) const {
    return "L." + functionName_ + "." + name;
}
void Tms6747::defineLabel(const std::string &l) { out_ << l << ":\n"; }

// Every branch on the C6000 has five delay slots; fill them with a NOP for now.
void Tms6747::jump(const std::string &l) {
    out_ << "\tB\t" << l << "\n\tNOP\t5\n";
}
void Tms6747::branchIfZero(const std::string &) { unsupported("a conditional branch"); }
void Tms6747::branchIfNotZero(const std::string &) { unsupported("a conditional branch"); }
void Tms6747::caseBranch(long long, const std::string &) { unsupported("a switch"); }
void Tms6747::genTruth(const Expr &) { unsupported("a truth test"); }
void Tms6747::landingPad(int, int) { unsupported("a C++ landing pad"); }

void Tms6747::visit(const Num &n) {
    if (n.type()->isFloating()) unsupported("a floating-point constant");
    movImm("A4", n.value());
}
void Tms6747::visit(const Var &) { unsupported("a variable reference"); }
void Tms6747::visit(const Assign &) { unsupported("an assignment"); }
void Tms6747::visit(const Unary &) { unsupported("a unary operator"); }
void Tms6747::visit(const Binary &) { unsupported("a binary operator"); }
void Tms6747::visit(const Postfix &) { unsupported("a postfix operator"); }
void Tms6747::visit(const Call &) { unsupported("a function call"); }
void Tms6747::visit(const Cast &) { unsupported("a cast"); }
void Tms6747::visit(const StrLit &) { unsupported("a string literal"); }
void Tms6747::visit(const VaStart &) { unsupported("va_start"); }
void Tms6747::visit(const VaArg &) { unsupported("va_arg"); }
void Tms6747::visit(const MemberAccess &) { unsupported("a member access"); }

void Tms6747::visit(const Return &n) {
    markLine(n);
    if (n.hasValue()) {
        if (n.value().type()->isStructOrUnion()) unsupported("returning a struct");
        n.value().accept(*this);   // the result lands in A4
    }
    jump(returnLabel_);
}

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

    if (fn.sretSlot() != 0 || fn.isVariadic() || !fn.locals().empty())
        unsupported("a function with locals, a struct return, or varargs");

    frame_ = (fn.frameSize() + 7) / 8 * 8;
    if (frame_ > 0) out_ << "\tSUB\tB15, " << frame_ << ", B15\n";

    fn.body().accept(*this);

    out_ << returnLabel_ << ":\n";
    if (frame_ > 0) out_ << "\tADD\tB15, " << frame_ << ", B15\n";
    out_ << "\tB\tB3\n\tNOP\t5\n";
}

void Tms6747::run(const Program &program) {
    emitData(program);
    for (const Function &fn : program.functions) emitFunction(fn);
    sink_ << out_.str();
}
