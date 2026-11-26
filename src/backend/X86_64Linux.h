#pragma once

#include "Backend.h"
#include "Dwarf.h"
#include "../optimizer/Inliner.h"
#include "../optimizer/Optimizer.h"
#include "Spelling.h"
#include "Walker.h"

#include <iosfwd>
#include <map>
#include <memory>
#include <sstream>
#include <set>
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
    std::unique_ptr<CodeGen> codegen(std::ostream &sink, Syntax syntax) const override;
    bool emitsLineTable(Syntax) const override { return true; }
    bool optimizesAroundVolatile() const override { return true; }
private:
    LinuxX86_64Target target_;
};

class X86_64Linux : public Walker {
public:
    // **The COFF spelling where the names are Microsoft's.**
    X86_64Linux(std::ostream &sink, const Target &target, const Abi &abi)
        : target_(target), sink_(sink), abi_(abi) {
        if (target.microsoftNames()) a_ = &coff_;
        plain_ = a_;
    }

    using Walker::visit;
    void run(const Program &program) override;
    void setOptimize(int level) override;

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
    // -g's records: CodeView for Microsoft's tools, DWARF elsewhere; the MASM path spells the same
    // CodeView for GRIDE's masm (1.29).
    virtual void writeDebug(const std::vector<DwarfFunction> &fns,
                            const std::vector<DwarfGlobal> &globals);
    // **The MASM path writes its own RTTI records**, so the COFF ones must not.
    virtual bool emitsOwnRtti() const { return false; }

    // **The exception model follows the target, not the spelling.**
    bool usesFunclets() const override { return target_.microsoftNames(); }
    bool terminateScopes() const override { return !target_.microsoftNames(); }
    std::string terminatePad(int id) override;
    std::string beginFunclet(std::size_t pos, int number, bool cleanup) override;
    void funcletLeave(const std::string &label) override;
    void endCleanupFunclet() override;
    void endFunclet(const std::string &resume) override;
    void storeUnwindHelp(int slot) override;
    void closeFunclet(const std::string &tail);
    // A Windows local is `frameSize - slot` above the establisher frame, which
    // is the whole translation between how cxx1 addresses a local and how an
    // FH3 table describes one.
    int establisherOffset(int slot) const { return frameSize_ + outgoing_ - slot; }
    void emitCoffEhTables(const Function &fn);   // the FH3 tables, one state tree
    // The five objects the Microsoft ABI wants per class with a vftable.
    void emitCoffClassRtti(const Program &program);
    // The four objects a Microsoft throw is identified by, in GNU syntax.
    void emitCoffThrowInfo(const Program &program);
    // Opens one of those records as a COMDAT of its own, public, so that the
    // linker keeps one copy where every unit that names the type wrote one.
    void coffRecord(const char *section, const std::string &label, int p2align,
                    const char *flags = "dr");

    // A funclet is written by walking the handler into the ordinary output and
    // lifting the text back out - what the body appended, in order, IS the
    // funclet, so moving it costs no second code path.
    std::string funclets_;
    std::vector<std::size_t> funcletMarks_;
    int funcletIndex_ = 0;
    std::string funcletSymbol_;
    // The funclets the one being walked was opened inside, with their entries' -g locations.
    std::vector<std::pair<std::string, std::string> > outerFunclets_;
    // The Microsoft type descriptors this file has laid down, so a class both
    // described for RTTI and thrown gets its `??_R0` once.
    std::set<std::string> msDescriptors_;
    const char *funcletKind_ = "$catch$";
    // What this file defines; any other symbol's address comes from the GOT on ELF, as PIE requires.
    std::set<std::string> definedHere_;
    // The function being emitted, which the tables and funclets name.
    std::string fnSymbol_;
    // Whether the function being emitted went into a COMDAT, which its funclets and their unwind data have to join - see closeFunclet.
    bool fnMergeable_ = false;
    // **A funclet's .pdata goes last, after every ordinary function's.**
    std::string funcletPdata_;
    // **-g describes a funclet as a procedure of its own**: opened with it,
    // after its function's record, taking the lines until it closes;
    // `funcletEntryLoc_` is its first instruction's (the `catch` line).
    void funcletDebugBegin(const std::string &symbol, std::size_t pos, int number, bool cleanup);
    void funcletDebugEnd();
    std::string funcletEntryLoc_;
    // The function being emitted, among dwarfFns_: its funclets' records follow it.
    std::size_t dwarfFnAt_ = 0;

    std::string out_;
    std::size_t emittedSize() override { return out_.size() + (optimizer_ ? optimizer_->held() : 0); }
    Spelling *a_ = &gnu_;
    // In front of a_ at -O1 and above. It holds a function whole, its funclets apart, so anything that lifts text out of out_ goes through atOutput.
    std::unique_ptr<Optimizer> optimizer_;
    // Done where it stands in the output: at once, or after what an optimizer holds.
    void atOutput(std::function<void()> f) { if (optimizing()) optimizer_->defer(std::move(f)); else f(); }

    void landingPad(int pointerSlot, int selectorSlot) override;
    void exceptionRegion(const std::string &begin, const std::string &end, const std::string &target) override;

protected:
    // The `.gcc_except_table` for the function just emitted, in its COMDAT group when mergeable.
    void emitLsda(const std::string &symbol, bool mergeable);

    // **Where the frame base sits, relative to the locals.**
    virtual bool localsAboveFrameBase() const { return target_.microsoftNames(); }

    // One local, written against rbp as Itanium establishes it, so a target whose base is elsewhere moves
    // every slot by one constant where operands are rendered; **a realigned frame is addressed from its
    // floor in rbx**, slot k at rbx + top - k with the top rounded to the alignment (Function::frameAlign).
    Op local(long long slot) const { return realign_ > 0 ? mem(top_ - slot, "%rbx") : mem(-slot, "%rbp"); }
    Op frameSlot(long long disp) const { return disp < 0 ? local(-disp) : mem(disp, "%rbp"); }   // below rbp a slot, above it the caller's
    int realign_ = 0;
    // The function being written reads or writes a volatile lvalue, so the optimizer is passed by (emit).
    bool volatileHere_ = false;
    // Which callees touch one, asked once each: such a callee is never walked in place.
    mutable std::map<const Function *, bool> volatileBodies_;
    int top_ = 0;
    Spelling *plain_ = nullptr;   // under the optimizer, written to directly for a realigned frame - see emit()
    bool optimizing() const { return optimizer_ && a_ == optimizer_.get(); }
    int frameSize_ = 0;
    // **The shadow space, allocated once at the floor of the frame** rather than around each call; 0 where the ABI has none or the body calls nothing. A call finds it at rsp only when nothing is pushed above it.
    int outgoing_ = 0;
    // How far below the floor rsp stood when a callee began to be walked in place; that walk counts depth_ from zero again.
    int floorDepth_ = 0;
    // Inside a funclet, whose own frame holds 32 bytes and no more.
    bool inFunclet_ = false;

    // Whatever this target writes after a function to describe its handlers.
    virtual void emitExceptionTables(const Function &fn) {
        // Microsoft frames carry FH3 tables, not an LSDA.
        if (target_.microsoftNames()) {
            // One table for cleanups and tries alike, nested or not: a state tree.
            if (msStates().empty() && fn.msUnwindHelp() == 0) {
                out_ += funclets_; funclets_.clear(); funcletIndex_ = 0;
            } else {
                emitCoffEhTables(fn);
            }
            return;
        }
        if (!callSites().empty()) emitLsda(fn.symbol(), fn.isInline());
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
    std::string returnLabel_;
    // In a funclet too (1.29): its lines are a table of the funclet's own, since its code is in another section - see Spelling::funcletLinesBegin.
    void emitLoc(int file, int line, int column) override { a_->location(file, line, column); }
    void defineLabel(const std::string &l) override;
    void defineStateLabel(const std::string &l) override;
    void defineBlockLabel(const std::string &l) override;
    void jump(const std::string &l) override;
    void branchIfZero(const std::string &l) override;
    void branchIfNotZero(const std::string &l) override;
    void caseBranch(long long v, const std::string &l) override;
    std::string labelPrefix_;
    int sretSlot_ = 0;
    int regSave_ = 0;
    int varGp_ = 0, varFp_ = 48, varOverflow_ = 16;

    void emit(const Function &fn);
    void receiveParameters(const Function &fn);
    void walkBody(const Function &fn);
    std::vector<opt::Local> scalarsOf(const Function &fn) const;
    // Inline expansion - see inlineTarget; the inliner decides, by the
    // level's costs, which sites are worth it.
    const Function *inlineTarget(const Call &n, int stackSlots) const;
    void walkInPlace(const Function &fn);
    std::unique_ptr<Inliner> inliner_;
    std::map<std::string, const Function *> bodies_;
    const Function *current_ = nullptr;
    bool inPlace_ = false;
    bool inlining() const;
    int inlineReserve_ = 0;
    void finishChunk();
    void vaArgClass(const VaArg &n);
    std::string label(const char *kind, int id) const override;
    std::string userLabel(const std::string &name) const override;
    void emitData(const Program &program);
    void emitGlobal(const Global &g, Segment seg);
    // A thread_local's address into rax: local-exec or its wrapper on ELF, cl's `_tls_index` walk on
    // COFF with the CRT's guard asked first where the object may still want initialising.
    void tlsAddress(const Var &v);
    void msTlsBlock();
    // The `_ZTW` wrappers and `_ZTH` aliases this unit defines - ELF only, as raw text after the functions.
    void emitTlsWrappers(const Program &program);
    void push();
    void pop(const char *into);
    // A stack argument, on the real stack whatever the level: the call unwinds it.
    void pushArg();
    // **A temporary goes to a frame slot rather than the stack** where the
    // optimizer holds the function and no funclet reads the frame, so depth_
    // counts the real stack alone and the pads and unwind data stay the walker's.
    bool tempsAllowed_ = false;
    int tempDepth_ = 0;
    int tempHigh_ = 0;
    bool tempsInSlots() const { return tempsAllowed_ && !inFunclet_ && optimizing(); }
    // One stack per function, inlined callees included, placed below every frame once the walk is done.
    Op tempSlot(int t) const { return local(Optimizer::kTempBase + 8 * (t + 1)); }
    void pushF();
    void popF(const char *into);
    void pushFArg();

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

    // **Where one argument goes, decided once for both ends of the call.**
    struct ArgPlace {
        std::vector<bool> lanes;   // empty when the argument travels in memory
        std::vector<int> regs;     // one register slot per lane
        bool inMemory = false;
        bool padBelow = false;     // the caller pushes 8 bytes under this one
        int stackOffset = 0;       // bytes from the base the callee supplies
        int stackWords = 0;        // and how many 8-byte words it occupies
    };

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
    // Whether a call's result comes back through a hidden pointer, and the
    // whole call placed - what visit(Call) and the outgoing area both ask.
    bool returnsThroughPointer(const Call &n) const;
    Placement placeCall(const Call &n) const;
    // Whether `e` may call at the floor and write the outgoing area - or move the floor, by an `_alloca`
    bool mayWriteArea(const Expr &e) const;
    // `_alloca(n)`: rsp moves down by n rounded to 16, probed first on Windows, and the block sits above the
    // outgoing area and whatever this function has pushed, both of which move down with rsp.
    void genAlloca(const Call &n);
};

std::vector<bool> classifyEightbytes(const Type *t, const Target &target);
