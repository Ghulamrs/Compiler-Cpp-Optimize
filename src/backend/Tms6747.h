#pragma once

// The TMS320C6747 (C674x, C6000 family) as a code-generation target: C6000
// assembly text only, serial, a stack machine over A4, delay slots as NOPs.
// The conventions, the ABI as emitted and the milestones: VM6747/TMS6747.md.

#include "Backend.h"
#include "Walker.h"

#include "../optimizer/Inliner.h"
#include "../optimizer/OptCosts.h"

#include <iosfwd>
#include <map>
#include <memory>
#include <sstream>
#include <string>

class Tms6747Target final : public Target {
public:
    int sizeOf(Kind) const override;
    int alignOf(Kind) const override;
    bool plainCharIsSigned() const override { return true; }
    Kind sizeType() const override { return Kind::UInt; }
    Kind wcharType() const override { return Kind::UShort; }   // 16-bit and unsigned: TI, measured
    bool resumeTakesException() const override { return false; }   // __cxa_end_cleanup(), measured from cl6x
    bool hasGetExceptionPtr() const override { return false; }     // not in rts6740_elf_eh.lib
    int stackAlign() const override { return 8; }
    bool loadsUnaligned() const override { return false; }   // LDW faults; no LDNW is emitted
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
    void visit(const While &) override;
    void visit(const For &) override;
    void visit(const DoWhile &) override;
    void visit(const Return &) override;
    void landingPad(int pointerSlot, int selectorSlot) override;
    bool terminateScopes() const override { return true; }
    void setOptimize(int level) override;

private:
    std::ostringstream out_;    // the piece being emitted (one function at a time)
    std::string file_;          // the finished pieces, in order
    int optimize_ = 0;          // -O1 and -O2 run the body through c6xSchedule
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
    int pushDepth_ = 0;                       // pairs pushed and not yet popped, which fill *B15
    std::vector<int> areas_;                  // the call areas open, innermost last
    bool usesSavedArgRegs_ = false;
    bool usesSavedPairRegs_ = false;   // A11/B11/A13/B13, written by a 64-bit argument in A10-B12
    int frame_ = 0;                           // the locals, 8-aligned
    bool needsUnexpected_ = false;            // a noexcept function's table names __cxa_call_unexpected
    bool needsPr2_ = false;                   // a table past 64 KB names __c6xabi_unwind_cpp_pr2
    bool anyExidx_ = false;                   // a function got an exception-index entry, so the personality is named
    std::vector<std::string> savedRegs() const;
    unsigned unwindWord(bool needFrame) const;
    std::vector<unsigned> longUnwindWords() const;
    int sretSlot_ = 0;                        // where the caller's result pointer is kept
    std::size_t sretShift_ = 0;               // 1 when that pointer is A4 and the parameters start at B4
    std::size_t firstStack_ = 0;              // the first parameter passed on the stack
    int vaStart_ = 0;                         // the unnamed arguments, above the caller's B15

    // Inlining at -O2: the inliner's decisions, the unit's bodies by symbol, and where a callee
    // walked in place keeps its locals - past the caller's own frame, which grows to hold them.
    std::unique_ptr<opt::Costs> costs_;
    std::unique_ptr<Inliner> inliner_;
    std::map<std::string, const Function *> bodies_;
    int inlineDepth_ = 0;                     // callees walked in place around this point
    int localBase_ = 0;                       // added to every frame offset: 0, or the callee's place
    int inlineTop_ = 0;                       // past the innermost callee's locals: where the next one nests
    int inlineBase_ = 0;                      // the caller's own frame, where an inlined callee starts
    const Function *functionOf_ = nullptr;    // the function being emitted, for the declines
    const Function *inlineTarget(const Call &n) const;
    void walkInPlace(const Function &fn);

    // Locals in registers at -O1 and -O2: the body is walked twice, the first time to learn which
    // scalar locals never have their address formed and how often each is used, the second with
    // the most used of those in A10-A13 and B10-B13, a 64-bit one in an even:odd pair.
    struct Slot { int uses = 0; bool addressed = false; bool wide = false; int size = 0; int extent = 0; int align = 0; int winUses = 0; };
    std::map<int, Slot> slots_;                // by frame offset, an inlined callee's locals included
    std::map<int, std::string> regOf_;         // offset -> the register (the low one of a pair)
    std::vector<std::string> promoted_;        // those registers, to be saved and restored
    // A push whose pop lies past a call keeps its value in a spare callee-saved register: the memory
    // it would go through is an L1D stall on the C6747. The first walk records which pushes cross a
    // call; planSpills replays them to learn the registers, and the second walk hands them out.
    std::vector<bool> pushCrossesCall_;
    std::vector<int> pushEvents_;              // the first walk: +1 a word push, +2 a pair push, -1 a pop
    std::vector<int> pushLoop_;                // per push, the loop depth it was met at
    std::vector<std::pair<std::size_t, int> > pushOpen_;   // (seq, calls so far) per open push, first walk
    std::size_t pushSeq_ = 0;
    int callCount_ = 0;
    bool leaf_ = false;                        // the first walk found no call and no loop: locals go in A16-A23
    bool hasLoop_ = false;
    bool paramInFrame_ = false;                // a parameter reached through A15
    std::vector<std::string> spillPool_;
    std::vector<std::string> spillHeld_;       // per open push: its register, "" for the stack
    std::vector<std::string> spillUsed_;       // the registers the replay chose, saved with promoted_
    bool simpleOfParams(const Expr &e, const Function &fn) const;
    std::string earlyExit(const Function &fn);
    std::string chooseSpill(bool wide);
    void planSpills();
    bool spillPush(bool wide);
    bool spillPop(bool wide, const char *reg);
    bool planning_ = false;                    // the first walk: counting, and deciding nothing
    // The frame laid out by hotness at -O1 and -O2 (review B8): the most used slot nearest A15, so
    // it takes the *-A15(k) form; every slot the first walk reached has a place, and the saved
    // registers' band under A15 is only as wide as the function's own saves.
    std::map<int, int> place_;                 // key -> the slot's offset in the laid-out frame
    bool relayout_ = false;
    int placedTop_ = 0;
    int saveBytes_ = 40;
    void noteExtent(int key, int size, int align);
    void planParams(const Function &fn);
    void planLocals(const Function &fn);
    void planFrame();
    int placed(int key) const;
    bool plainAccess_ = false;                 // a read or write of a whole local, not an address
    int loopDepth_ = 0;                        // a use inside a loop weighs more
    bool regCandidate(const Var &v) const;     // a whole scalar local of this function, up to 8 bytes
    const std::string *regFor(const Var &v);   // its register on the second walk, or null
    void noteUse(int key, const Type *t);      // key: the frame offset, an inlined callee's past its caller's
    void planRegisters(const Function &fn);
    void regRead(const std::string &r, bool wide);    // A4 (A5:A4) = the register
    void regWrite(const std::string &r, bool wide);   // the register = A4 (A5:A4)
    static std::string pairHigh(const std::string &r);
    void walkBody(const Function &fn);

    std::size_t emittedSize() override { return static_cast<std::size_t>(out_.tellp()); }
    void defineLabel(const std::string &l) override;
    void jump(const std::string &l) override;
    void branchIfZero(const std::string &l) override;
    void branchIfNotZero(const std::string &l) override;
    void caseBranch(long long v, const std::string &l) override;
    bool wideSwitch_ = false;      // the switch's value is in A5:A4
    // A5:A4 shifted by a constant, the pair's halves spliced through A3.
    void shiftPairLeft(int count);
    void shiftPairRight(int count, bool sign);
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
    void copyBlock(int size, const char *from, const char *to, int align, bool wide = false, const char *pred = "");
    void copyBlockBatched(int size, const char *from, const char *to, int align, bool wide, const char *pred);
    bool inPair(const Type *t) const;         // a struct of 8 bytes or less: in registers, argument or result
    bool inPairWide(const Type *t) const;     // and one of 5 to 8 takes the pair
    bool hiddenInA4(const Type *t) const;     // a class non-trivial for calls: its result pointer is the first parameter
    void loadPair(int size, int align);       // A5:A4 = the struct at *A4
    void storePair(int size, int align);      // the struct at *A3 = A5:A4
    void bitFieldUnitAddr(const MemberAccess &m);   // the unit's address -> A4
    void bitFieldExtract(const MemberAccess &m);    // unit in A4 -> the field
    void bitFieldInsert(const MemberAccess &m);     // value in A4 -> unit at *A6
    void spAdjust(int delta);
    bool immediateBinary(const Binary &n);
    void openArea(int area, bool stackArgs = false);
    void closeArea();                 // B15 += delta (negative allocates)
    void localAddr(int off, const char *dst, int size = 4, int align = 4); // dst = A15 - off
    bool frameSlot(const Expr &e, int &disp);   // a local or a member of one: its displacement from A15
    static bool frameFits(int disp, int size);        // whether frameOperand can name it
    std::string frameOperand(int disp, int size);     // *-A15(k), *+A15(k), or the scaled form through A0
    int accessSize(const Type *t) const;              // the bytes one load or store of t moves
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
    bool constDivisor(const Binary &n, long long &d) const;   // -O1 and up: a 32-bit / or % by a constant
    void divideByConstant(bool quotient, bool sign, long long d);   // A4 = A4 / d or A4 % d, no helper
    void wideCast(const Type *from, const Type *to);
    int stackParamOffset(const std::vector<Param> &ps, std::size_t i);
    int stackArg(const Type *t, int &end);
    std::string stackArgAccess(const Type *t, bool store, const std::string &mem);
    void genAddr(const Expr &e);              // address of an lvalue -> A4
    void load(const Type *t);                 // [A4] -> A4
    void loadFrom(const Type *t, const std::string &mem);   // mem -> A4
    void store(const Type *t, const std::string &mem);      // A4 -> mem
    void narrowInt(const Type *t);            // truncate A4 to t's width
    void emitGlobal(const Global &g, Segment seg);
    void emitData(const Program &program);
    void emitParams(const Function &fn);
    void emitExceptionTable(const Function &fn);
    void emitFunction(const Function &fn);
};
