#pragma once

// The TMS320C6747 (C674x, C6000 family) VLIW DSP, as a code-generation target.
// Output is C6000 assembly text only (-S). It is emitted the simple way the
// other backends emit: a stack-machine over a primary register (A4), serial -
// one instruction per execute packet, no || - with branch and load delay slots
// filled by NOP. Correct, not fast; VLIW scheduling is a later concern.
//
// Milestones: (1) integer-constant returns; (2) locals, assignments, integer
// arithmetic/comparison/bitwise/shift/logical, unary, postfix ++/--, and
// if/while/for with real return values; (3) parameters and calls under the
// C6000 EABI, globals, string literals and integer casts; (4) variadic calls,
// integer division through the EABI helpers, structs, bit-fields, floating
// point, 64-bit integers and variadic functions - this file.
//
// Floating point is the C674x's own: single precision in A4, double in the
// pair A5:A4, with the SP/DP instructions and their delay slots as NOPs;
// division and float-to-unsigned through the EABI helpers. A long long
// rides in the same pair and is done in 32-bit halves, its division and its
// conversions to and from floating point through the helpers too.
//
// Structs go by address: a struct value in A4 is where it lives. An argument
// is the address of a copy the caller makes; a result is written through the
// pointer the caller passes in A3.
//
// The ABI as emitted: the first ten word-sized arguments ride in A4, B4, A6,
// B6, A8, B8, A10, B10, A12, B12, the rest on the stack above the reserved
// word at *B15 (the first at B15+4) - and for a variadic callee, the last
// named argument and everything after it go on the stack, where va_start can
// walk them; the result comes back in A4; B3 holds the return address. A10-A15 and B10-B15 are callee-saved, so a function that
// loads A10/B10/A12/B12 for a call of its own saves them beside A15 and B3 in
// its frame link.
//
// The asm uses only unambiguous forms - no functional-unit specifiers (the
// assembler assigns them), zero-offset *reg loads and stores with the address
// computed into a register first, A1 as the branch predicate, NOP 4 after every
// load and NOP 5 after every branch. It is built to C6000 conventions but is
// not checked against a real assembler; there is none on these machines.

#include "Backend.h"
#include "Walker.h"

#include <iosfwd>
#include <sstream>
#include <string>

class Tms6747Target final : public Target {
public:
    int sizeOf(Kind) const override;
    int alignOf(Kind) const override;
    bool plainCharIsSigned() const override { return true; }
    Kind sizeType() const override { return Kind::UInt; }
    Kind wcharType() const override { return Kind::Int; }
    bool microsoftNames() const override { return false; }
    const char *name() const override { return "tms6747"; }
};

class Tms6747Backend final : public Backend {
public:
    const char *name() const override { return "tms6747"; }
    const Target &target() const override { return target_; }
    const Abi &abi() const override;
    bool emits() const override { return true; }
    const char *const *identityMacros() const override;
    std::unique_ptr<CodeGen> codegen(std::ostream &sink, Syntax) const override;
    bool emitsLineTable(Syntax) const override { return false; }
private:
    Tms6747Target target_;
};

class Tms6747 final : public Walker {
public:
    Tms6747(std::ostream &sink, const Target &target, const Abi &abi)
        : sink_(sink), target_(target), abi_(abi) {}

    using Walker::visit;
    void run(const Program &program) override;

    void visit(const Num &) override;
    void visit(const Var &) override;
    void visit(const Assign &) override;
    void visit(const Unary &) override;
    void visit(const Binary &) override;
    void visit(const Postfix &) override;
    void visit(const Call &) override;
    void visit(const Cast &) override;
    void visit(const StrLit &) override;
    void visit(const VaStart &) override;
    void visit(const VaArg &) override;
    void visit(const MemberAccess &) override;
    void visit(const Switch &) override;
    void visit(const Return &) override;
    void landingPad(int pointerSlot, int selectorSlot) override;

private:
    std::ostringstream out_;    // the piece being emitted (one function at a time)
    std::string file_;          // the finished pieces, in order
    std::ostream &sink_;
    const Target &target_;
    const Abi &abi_;

    std::string functionName_;
    std::string returnLabel_;
    std::string labelPrefix_;

    // Gathered while the body is emitted, then used to shape the prologue: a
    // function that calls saves B3, and one that loads the callee-saved
    // argument registers (A10/B10/A12/B12, arguments 7-10) saves those too.
    bool hasCall_ = false;
    bool usesSavedArgRegs_ = false;
    int linkBytes_ = 8;                       // saved A15 + B3 (+ the four above)
    int sretSlot_ = 0;                        // where the caller's A3 is kept
    std::size_t firstStack_ = 0;              // the first parameter passed on the stack
    int vaStart_ = 0;                         // the unnamed arguments, above the caller's B15

    std::size_t emittedSize() override { return static_cast<std::size_t>(out_.tellp()); }
    void defineLabel(const std::string &l) override;
    void jump(const std::string &l) override;
    void branchIfZero(const std::string &l) override;
    void branchIfNotZero(const std::string &l) override;
    void caseBranch(long long v, const std::string &l) override;
    void genTruth(const Expr &e) override;
    std::string label(const char *kind, int id) const override;
    std::string userLabel(const std::string &name) const override;

    void unsupported(const char *what);
    void movImm(const char *reg, long long value);
    static std::string symName(const std::string &sym);
    void movSym(const char *reg, const std::string &sym);
    void regAdd(const char *base, int off, const char *dst); // dst = base + off
    void call(const std::string &target);     // B3 = return address; B target
    void genArg(const Call &n, std::size_t i);   // argument i -> A4
    void addOffset(int bytes);                // A4 += bytes
    void copyBlock(int size, const char *from, const char *to, int align);
    void bitFieldUnitAddr(const MemberAccess &m);   // the unit's address -> A4
    void bitFieldExtract(const MemberAccess &m);    // unit in A4 -> the field
    void bitFieldInsert(const MemberAccess &m);     // value in A4 -> unit at *A6
    void spAdjust(int delta);                 // B15 += delta (negative allocates)
    void localAddr(int off, const char *dst); // dst = A15 - off
    void push();                              // push A4
    void pop(const char *reg);                // reg = top; SP += 8
    bool isDouble(const Type *t) const;       // a 64-bit floating type
    bool isWide(const Type *t) const;         // any 64-bit scalar: it rides in A5:A4
    static std::string pairOf(const char *reg);  // "A4" -> "A5:A4"
    void pushValue(const Type *t);            // push the accumulator, 4 or 8 bytes
    void popValue(const Type *t, const char *reg);
    void moveValue(const Type *t, const char *reg);  // accumulator -> reg (pair)
    void fpConst(const Type *t, double v, const char *reg);
    void isZero(const Type *t);               // A4 = (accumulator == 0)
    void fpBinary(const Binary &n, bool dp);  // operands in A5:A4 / A7:A6
    void wideBinary(const Binary &n);         // 64-bit integers, the same places
    void wideCast(const Type *from, const Type *to);
    int stackParamOffset(const std::vector<Param> &ps, std::size_t i);
    void genAddr(const Expr &e);              // address of an lvalue -> A4
    void load(const Type *t);                 // [A4] -> A4
    void store(const Type *t, const char *addrReg);  // A4 -> [addrReg]
    void narrowInt(const Type *t);            // truncate A4 to t's width
    void emitGlobal(const Global &g, Segment seg);
    void emitData(const Program &program);
    void emitParams(const Function &fn);
    void emitExceptionTable(const Function &fn, int frameBytes);
    void emitFunction(const Function &fn);
};
