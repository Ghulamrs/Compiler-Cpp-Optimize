#pragma once

#include "Backend.h"
#include "Dwarf.h"
#include "Walker.h"

#include <iosfwd>
#include <sstream>
#include <set>
#include <string>
#include <vector>

class DarwinArm64Target final : public Target {
public:
    int sizeOf(Kind) const override;
    int alignOf(Kind) const override;
    bool plainCharIsSigned() const override { return true; }
    Kind sizeType() const override { return Kind::ULong; }
    Kind wcharType() const override { return Kind::Int; }
    bool microsoftNames() const override { return false; }
    bool armArrayCookie() const override { return true; }
    const char *name() const override { return "arm64-darwin"; }
};

class Arm64DarwinBackend final : public Backend {
public:
    const char *name() const override { return "arm64-darwin"; }
    const Target &target() const override { return target_; }
    const Abi &abi() const override;
    bool emits() const override { return true; }
    const char *const *identityMacros() const override;
    std::unique_ptr<CodeGen> codegen(std::ostream &sink, Syntax syntax) const override;
    bool emitsLineTable(Syntax) const override { return true; }
private:
    DarwinArm64Target target_;
};

class Arm64Darwin final : public Walker {
public:
    Arm64Darwin(std::ostream &sink, const Target &target, const Abi &abi)
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
    void visit(const Return &) override;

    void landingPad(int pointerSlot, int selectorSlot) override;
    bool terminateScopes() const override { return true; }
    std::string terminatePad(int id) override;

private:
    // The `.gcc_except_table` for the function just emitted: the call-site
    // table, the actions, and the type_info pointers the actions index.
    void emitLsda(const std::string &symbol);
    std::vector<std::string> lsdaTypes_;

    std::ostringstream out_;
    std::size_t emittedSize() override {
        return static_cast<std::size_t>(out_.tellp());
    }
    std::vector<DwarfFunction> dwarfFns_;
    std::vector<DwarfGlobal> dwarfGlobals_;
    std::ostream &sink_;
    const Target &target_;
    const Abi &abi_;

    std::string returnLabel_;
    std::string labelPrefix_;
    std::string functionName_;

    std::set<std::string> definedHere_;

    std::string label(const char *kind, int id) const override;
    void emitLoc(int file, int line, int column) override;
    void defineLabel(const std::string &l) override;
    void jump(const std::string &l) override;
    void branchIfZero(const std::string &l) override;
    void branchIfNotZero(const std::string &l) override;
    void caseBranch(long long v, const std::string &l) override;
    std::string userLabel(const std::string &name) const override;

    void unsupported(const char *what);

    void push();
    void pop(const char *reg);
    void pushD();
    void popD(const char *reg);
    void emitData(const Program &program);
    void emitGlobal(const Global &g, Segment seg);
    void emitFunction(const Function &fn);

    struct AggPlan {
        int hfa = 0;
        Kind elem = Kind::Double;
        int words = 0;
        bool byRef = false;
    };
    AggPlan planFor(const Type *t) const;
    void storeWord(const char *xreg, const char *base, int k, int size);
    int sretSlot_ = 0;

    int stackArgSlot(const Type *t, int &at) const;

    int aggStackSlot(const Type *t, const AggPlan &p, int &at) const;

    // **Where one argument goes, decided once for both ends of the call.** The
    // call site and the function's own prologue each walked the parameter list
    // by hand; two copies of one rule is the shape A-01 came out of on x86.
    struct ArgPlace {
        AggPlan plan;             // meaningful where the argument is a class
        bool inRegister = false;
        int firstReg = 0;         // the first x or d register it claims
        int stackOffset = -1;     // from the caller's base; the callee adds 16
    };
    struct Placement {
        std::vector<ArgPlace> args;
        int intsUsed = 0;
        int floatsUsed = 0;
        int stackBytes = 0;       // what the named arguments occupy
    };
    Placement placeArguments(const std::vector<const Type *> &types) const;

    void storeToStack(const Type *t, int off);
    int namedStackBytes_ = 0;

    void genAddr(const Expr &e);
    void addOffset(int bytes);
    void copyBlock(int size, const char *from, const char *to);
    void bitFieldUnitAddr(const MemberAccess &m);
    void bitFieldExtract(const MemberAccess &m);
    void bitFieldInsert(const MemberAccess &m);
    void load(const Type *t);
    void storeThrough(const Type *t, const char *addrReg);
    void genTruth(const Expr &e) override;
    void movImm(const char *reg, long long value);
    void loadFpConst(const std::string &reg, const Type *t, double v);
    void genConversion(const Type *from, const Type *to);
    void narrowInt(const Type *to);
};
