#pragma once

#include "Backend.h"
#include "Dwarf.h"
#include "Optimizer.h"
#include "Spelling.h"
#include "Walker.h"

#include <iosfwd>
#include <sstream>
#include <string>
#include <vector>

class LinuxX86_64Target final : public Target {
public:
    int sizeOf(Kind) const override;
    int alignOf(Kind) const override;
    bool plainCharIsSigned() const override { return true; }
    Kind sizeType() const override { return Kind::ULong; }
    Kind wcharType() const override { return Kind::Int; }
    bool microsoftNames() const override { return false; }
    const char *name() const override { return "x86_64-linux"; }
};

class X86_64LinuxBackend final : public Backend {
public:
    const char *name() const override { return "x86_64-linux"; }
    const Target &target() const override { return target_; }
    const Abi &abi() const override;
    bool emits() const override { return true; }
    const char *const *identityMacros() const override;
    std::unique_ptr<CodeGen> codegen(std::ostream &sink, bool gnuAsm) const override;
    bool emitsLineTable(bool) const override { return true; }
private:
    LinuxX86_64Target target_;
};

class X86_64Linux : public Walker {
public:
    // **The COFF spelling where the names are Microsoft's.**
    X86_64Linux(std::ostream &sink, const Target &target, const Abi &abi)
        : target_(target), sink_(sink), abi_(abi) {
        if (target.microsoftNames()) a_ = &coff_;
    }

    // **The IR goes in front of whichever spelling the target chose**, and only
    // when asked: at -O0 the walker speaks to the spelling directly, as before.
    void setOptimize(int level) override {
        level_ = level;
        if (level < 1) return;
        opt_.wrap(a_, level, abi_.variadicSseCountInAl, target_.microsoftNames());
        a_ = &opt_;
    }
    void loopHead() override { if (level_ >= 2) a_->loopAlign(); }

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

protected:

    virtual bool writesDwarf() const { return true; }
    // **The MASM path writes its own RTTI records**, so the COFF ones must not.
    virtual bool emitsOwnRtti() const { return false; }

    // **The exception model follows the target, not the spelling.**
    bool usesFunclets() const override { return target_.microsoftNames(); }
    bool terminateScopes() const override { return !target_.microsoftNames(); }
    std::string terminatePad(int id) override;
    void regionEnd() override;
    std::string beginFunclet() override;
    void endCleanupFunclet() override;
    void endFunclet(const std::string &resume) override;
    void storeUnwindHelp(int slot) override;
    void closeFunclet(const std::string &tail);
    // A Windows local is `frameSize - slot` above the establisher frame, which
    // is the whole translation between how cxx1 addresses a local and how an
    // FH3 table describes one.
    int establisherOffset(int slot) const { return frameSize_ - slot; }
    void emitCoffCleanupTables(const Function &fn);
    // The same tables for a frame that *catches*.
    void emitCoffTryTables(const Function &fn);
    // The five objects the Microsoft ABI wants per class with a vftable.
    void emitCoffClassRtti(const Program &program);
    // The four objects a Microsoft throw is identified by, in GNU syntax.
    void emitCoffThrowInfo(const Program &program);

    // A funclet is written by walking the handler into the ordinary output and
    // lifting the text back out - what the body appended, in order, IS the
    // funclet, so moving it costs no second code path.
    std::string funclets_;
    std::size_t funcletMark_ = 0;
    int funcletIndex_ = 0;
    std::string funcletSymbol_;
    // Nonzero inside a funclet, whose own frame offers 32 bytes and no more.
    int funcletDepth_ = 0;
    const char *funcletKind_ = "$catch$";
    // The function being emitted, which the tables and funclets name.
    std::string fnSymbol_;
    // Whether the function being emitted went into a COMDAT, which its funclets and their unwind data have to join - see closeFunclet.
    bool fnMergeable_ = false;
    // **A funclet's .pdata goes last, after every ordinary function's.**
    std::string funcletPdata_;

    std::string out_;
    // Measured after the IR has written out, so the count is of real text.
    std::size_t emittedSize() override { opt_.flush(); return out_.size(); }
    Spelling *a_ = &gnu_;
    // The optional IR in front of a_ - see setOptimize; flushed before out_ is read or cut.
    Optimizer opt_;
    // -O0, -O1 or -O2, for the choices the walker makes itself.
    int level_ = 0;

    void landingPad(int pointerSlot, int selectorSlot) override;

protected:
    // The `.gcc_except_table` for the function just emitted.
    void emitLsda(const std::string &symbol);

    // **Where the frame base sits, relative to the locals.**
    virtual bool localsAboveFrameBase() const { return target_.microsoftNames(); }

    // One local. Every frame-relative operand in this file is written against
    // rbp as Itanium establishes it, so a target whose base is elsewhere moves
    // all of them by one constant, applied once where operands are rendered.
    Op local(long long slot) const { return mem(-slot, "%rbp"); }
    int frameSize_ = 0;

    // Whatever this target writes after a function to describe its handlers.
    virtual void emitExceptionTables(const Function &fn) {
        opt_.flush();
        // Microsoft frames carry FH3 tables, not an LSDA.
        if (target_.microsoftNames()) {
            // **Cleanups and handlers never share a function**, which the
            // parser enforces on every target - so the first region decides
            // which shape of table this frame wants.
            if (msTries().empty()) {
                out_ += funclets_; funclets_.clear(); funcletIndex_ = 0;
            } else if (msTries()[0].isCleanup) {
                emitCoffCleanupTables(fn);
            } else {
                emitCoffTryTables(fn);
            }
            return;
        }
        if (!callSites().empty()) emitLsda(fn.symbol());
    }
    std::vector<std::string> lsdaTypes_;
    std::vector<std::string> lsdaStubs_;
    bool lsdaPersonality_ = false;
    // Reachable from the MASM subclass, which needs the target to size the objects the Microsoft ABI wants a throw to carry.
    const Target &target_;

private:
    std::vector<std::string> chunks_;
    std::vector<DwarfFunction> dwarfFns_;
    std::vector<DwarfGlobal> dwarfGlobals_;
    std::ostream &sink_;
    GnuSpelling gnu_{out_};
    CoffSpelling coff_{out_};

    const Abi &abi_;
    int depth_ = 0;
    int outgoing_ = 0;   // the widest call's, sized before the body - Spelling::prologue
    // Nonzero from a call's first write into the area until its `call`, so a
    // call nested in a later argument takes the pushing road below it.
    int areaBusy_ = 0;
    std::string returnLabel_;
    void emitLoc(int file, int line, int column) override { a_->location(file, line, column); }
    void defineLabel(const std::string &l) override;
    void jump(const std::string &l) override;
    void branchIfZero(const std::string &l) override;
    void branchIfNotZero(const std::string &l) override;
    void caseBranch(long long v, const std::string &l) override;
    std::string labelPrefix_;
    int sretSlot_ = 0;
    int regSave_ = 0;
    int varGp_ = 0, varFp_ = 48, varOverflow_ = 16;

    void emit(const Function &fn);
    void finishChunk();
    std::string label(const char *kind, int id) const override;
    std::string userLabel(const std::string &name) const override;
    void emitData(const Program &program);
    void emitGlobal(const Global &g, Segment seg);
    void push();
    void pop(const char *into);
    void pushF();
    void popF(const char *into);

    void pushX87();
    void popX87();

    bool isX87(const Type *t) const { return t->isX87(target_); }

    Kind genKind(const Type *t) const;

    void loadX87Const(long double v);
    void x87ToInt(const Type *to);
    void intToX87(const Type *from);
    void genX87Binary(const Binary &n);

    void genAddr(const Expr &e);

    void load(const Type *t);
    void store(const Type *t);
    void storeAt(const Type *t, int offset);
    void bitFieldUnitAddr(const MemberAccess &m);
    void bitFieldExtract(const MemberAccess &m);
    void bitFieldInsert(const MemberAccess &m);

    void copyBlock(int size);
    void copyBlockCompact(int size);

    void canonicalise(const Type *t);
    void genFloatBinary(const Binary &n);
    void genConversion(const Type *from, const Type *to);
    void genTruth(const Expr &e) override;

    const char *acc(const Type *t) const;
    const char *rhs(const Type *t) const;

    void unsupported(const char *what);

    // **The last lane of an aggregate is composed, never approximated.** These
    // three write and read exactly `left` bytes and never touch a byte past the
    // object, where one widened move took whatever the destination held.
    void storeTailFromReg(const char *reg64, long long off, const char *base,
                          int left);          // clobbers reg64
    void copyTailMem(long long from, long long to, int left);   // via %rax
    void loadTailToReg(const char *reg64, long long off, const char *base,
                       int left);             // clobbers %rcx
    void msAggregateToRax(const Type *t, int slot);
    void msCopyToSlot(const Type *t, int slot, const char *from);
    int takeSlot(bool sse, int &ints, int &sses) const;

    // Whether a call's result travels through a hidden pointer, asked in one place.
    bool returnsViaPointer(const Call &n) const;
    // The bytes a call wants at rsp: the shadow space and its stack arguments.
    int outgoingBytes(const Call &n) const;

    // **Where one argument goes, decided once for both ends of the call.**
    struct ArgPlace {
        std::vector<bool> lanes;   // empty when the argument travels in memory
        std::vector<int> regs;     // one register slot per lane
        bool inMemory = false;
        bool padBelow = false;     // the caller pushes 8 bytes under this one
        int stackOffset = 0;       // bytes from the base the callee supplies
        int stackWords = 0;        // and how many 8-byte words it occupies
    };
    // The value just computed (or an aggregate's address in rax) written to its place in the area.
    void storeOutgoing(const Type *t, const ArgPlace &p, int argSlot);

    // The whole list, in order. `sret` says a hidden return pointer is passed,
    // `hasThis` that the first argument is an object - between them they
    // decide which register the first written argument actually gets.
    struct Placement {
        std::vector<ArgPlace> args;
        int intsUsed = 0;
        int ssesUsed = 0;
        int stackWords = 0;
    };
    Placement placeArguments(const std::vector<const Type *> &types,
                             bool hasThis, bool sret) const;
};

std::vector<bool> classifyEightbytes(const Type *t, const Target &target);
