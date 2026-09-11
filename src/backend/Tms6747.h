#pragma once

// The TMS320C6747 (C674x, C6000 family) VLIW DSP, as a code-generation target
// for cxx1 - the C++ half of the VM6747 line. This mirrors cc1's tms6747
// backend; the only differences are cxx1's own backend shape (a gnuAsm flag on
// codegen/emitsLineTable, microsoftNames() on the target, and the landingPad
// hook C++ exceptions need). Output is C6000 assembly text only (-S).
//
// Emitted the simple way the other backends emit: a stack-machine over a
// primary register, serial (no ||), delay slots filled with NOP. Milestone 1
// stands the target up and returns integer constants; everything else calls
// unsupported() until its milestone.

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
    bool microsoftNames() const override { return false; }   // EABI, Itanium-style
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
    // target and abi arrive for the milestones that need them; milestone 1
    // needs neither, so they are accepted and ignored rather than stored unused.
    Tms6747(std::ostream &sink, const Target &target, const Abi &abi)
        : sink_(sink) { (void)target; (void)abi; }

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

    std::string functionName_;
    std::string returnLabel_;
    std::string labelPrefix_;
    int frame_ = 0;

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
    void emitData(const Program &program);
    void emitFunction(const Function &fn);
};
