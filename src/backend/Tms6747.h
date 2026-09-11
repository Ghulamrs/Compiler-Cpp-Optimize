#pragma once

// The TMS320C6747 (C674x, C6000 family) VLIW DSP, as a code-generation target.
// Output is C6000 assembly text only (-S). It is emitted the simple way the
// other backends emit: a stack-machine over a primary register (A4), serial -
// one instruction per execute packet, no || - with branch and load delay slots
// filled by NOP. Correct, not fast; VLIW scheduling is a later concern.
//
// Milestones: (1) integer-constant returns; (2) locals, assignments, integer
// arithmetic/comparison/bitwise/shift/logical, unary, postfix ++/--, and
// if/while/for with real return values - this file. Parameters and calls (the
// ABI), globals, casts, division, floats and structs are later milestones and
// call unsupported() until then.
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
    // abi arrives for the calls milestone; milestone 2 needs the target (for
    // type sizes and signedness) but not the abi, so the abi is ignored.
    Tms6747(std::ostream &sink, const Target &target, const Abi &abi)
        : sink_(sink), target_(target) { (void)abi; }

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
    void visit(const Return &) override;
    void landingPad(int pointerSlot, int selectorSlot) override;

private:
    std::ostringstream out_;
    std::ostream &sink_;
    const Target &target_;

    std::string functionName_;
    std::string returnLabel_;
    std::string labelPrefix_;

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
    void spAdjust(int delta);                 // B15 += delta (negative allocates)
    void localAddr(int off, const char *dst); // dst = A15 - off
    void push();                              // push A4
    void pop(const char *reg);                // reg = top; SP += 8
    void genAddr(const Expr &e);              // address of an lvalue -> A4
    void load(const Type *t);                 // [A4] -> A4
    void store(const Type *t, const char *addrReg);  // A4 -> [addrReg]
    void narrowInt(const Type *t);            // truncate A4 to t's width
    void emitData(const Program &program);
    void emitFunction(const Function &fn);
};
