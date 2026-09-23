#pragma once

#include "Spelling.h"

#include <functional>
#include <string>
#include <vector>

// **An instruction IR between the walker and the spelling.** The walker's
// instructions are kept in owned form until the function ends - or the text is
// read - rewritten, and only then handed to the real spelling; an Op's Str is a view, so it is copied.
struct IrOp {
    Op::Kind kind = Op::Reg;
    std::string text;
    long long disp = 0;
    bool hasDisp = false;
    unsigned long long uimm = 0;
    bool immNeg = false;
    bool immNumeric = false;

    static IrOp from(const Op &o);
    Op view() const;
    bool same(const IrOp &o) const;
};

// **What one instruction does to the machine**, as far as the passes need to
// know: which registers it reads, which it writes whole, which it writes in
// part - a part write keeps the rest, so it is a use as well - and the flags.

// Registers are canonical: every width of a name maps to the one 64-bit
// register it lives in, because a write to %eax is a write to %rax and a
// read of %al is a read of %rax.

// Bit i of a set is canonical register i: 0 rax, 1 rbx, 2 rcx, 3 rdx, 4 rsi,
// 5 rdi, 6 rbp, 7 rsp, 8-15 r8-r15, 16-31 xmm0-xmm15.
struct IrSem {
    enum Class {
        Unknown,  // a mnemonic not in the table: reads and writes everything
        Move,     // a is read, b is written and not read
        Rmw,      // a is read, b is read and written
        Cmp,      // both read, flags written
        Set,      // a written in part from the flags
        Unary,    // a read and written
        Push, Pop,
        Cqo, Cdq, Cltq,
        Div,      // rax:rdx in and out, a read
        X87,      // only memory and the x87 stack
        Call, Jump, Ret, Leave, Nop,
        Rep,      // rep movsq: rsi, rdi and rcx read, advanced, and fixed
    };
    Class cls = Unknown;
    unsigned use = 0;        // read, at any width, base registers included
    unsigned def = 0;        // written whole, and not read
    unsigned part = 0;       // written in part - also in `use`
    unsigned callee = 0;     // a call: what its own operand reads, `call *%r11`'s r11
    // A read that a rename may not touch: an implicit operand, the shift
    // count, the destination of a read-modify-write.
    unsigned fixed = 0;
    bool flagsUse = false;
    bool flagsDef = false;
    bool memWrite = false;
    // The register a Move or Pop writes whole, or -1; and its width.
    int dstReg = -1;
    int dstWidth = 0;
};

struct IrIns {
    std::string m;
    int operands = 0;
    IrOp a, b;
    // Set by a pass; the run is compacted before it is replayed.
    bool dead = false;
    // What the instruction does, computed once and kept while it stands:
    // a pass that changes an instruction invalidates it, or refreshes it.
    IrSem sem;
    bool semValid = false;
};

// **A basic block, with what surrounds it in the text.** A chunk starts at a
// label or after a terminator and ends at the next; the events are the spelling
// calls that fell between instructions, replayed where they came. The CFG fields are filled at flush.
struct IrChunk {
    std::vector<std::function<void()>> before;  // before the label
    std::string label;
    bool hasLabel = false;
    std::vector<std::function<void()>> after;   // after the label, before the code
    std::vector<IrIns> ins;
    int fall = -1;            // the chunk fallen into, or -1
    int target = -1;          // a jump's chunk, -1 for none, -2 for one not here
    unsigned gen = 0, kill = 0, liveIn = 0, liveOut = 0;
    bool flagsGen = false, flagsKill = false, flagsIn = false, flagsOut = false;
};

// **One of these per code generator, and so per file** - the driver compiles
// files on a thread pool, and nothing here is shared between two of them:
// every table below is a constant, every vector a member.
class Optimizer final : public Spelling {
public:
    Optimizer() = default;

    // Interpose in front of `under`; every call is forwarded there.
    /*  `callReads` says whether a call on this ABI may read the accumulator.
     *  System V does, at a variadic call, where AL carries the vector count;
     *  the Microsoft ABI passes nothing in rax and never does. Told `false`,
     *  the optimizer can see that a value moved out of rax before a call is
     *  dead at it - which is what lets retargetDefs fold
     *  `mov rax,X ; mov r8,rax` into `mov r8,X`. */
    void wrap(Spelling *under, int level, bool callReads = true, bool microsoftAbi = false) {
        under_ = under; level_ = level; callReadsAccumulator_ = callReads;
        microsoftAbi_ = microsoftAbi;
    }
    bool active() const { return under_ != nullptr; }

    // Write out what is buffered - called before the output text is read or cut.
    void flush();

    // How many instructions the passes removed, for anyone measuring.
    unsigned long removed() const { return removed_; }
    // Whether the function returns a second word in rdx: a ret reads rdx only then.
    void returnUsesRdx(bool yes) { rdxLive_ = yes; }
    // The body is complete: the next flush sees all of it and may promote.
    void endOfBody() { ending_ = true; }
    // The function's scalar locals by frame displacement and width - promote()'s candidates.
    void frameScalars(const std::vector<std::pair<long long, int> > &s) { scalars_ = s; }

    void ins(const std::string &m) override;
    void ins(const std::string &m, const Op &a) override;
    void ins(const std::string &m, const Op &a, const Op &b) override;

    void defLabel(const std::string &l) override;
    void functionBegin(const std::string &name, bool exported,
                       bool mergeable = false) override;
    void prologue(int frameSize, const std::string &lsda, unsigned saved,
                  int outgoing) override;
    void restoreSaved() override;
    void functionEnd(const std::string &name) override;
    void fileEntry(int n, const std::string &name) override;
    void location(int file, int line, int column) override;
    void predefine(const std::vector<std::string> &names) override;
    void preamble(std::ostream &o) override;
    void postamble(std::ostream &o) override;
    void globl(const std::string &name) override;
    void weakDefinition(const std::string &name) override;
    void textSection() override;
    void loopAlign() override;
    void rodataSection() override;
    void dataSection() override;
    void bssSection() override;
    void objectType(const std::string &name) override;
    void objectSize(const std::string &name, int size) override;
    void align(int n) override;
    void zero(int n) override;
    void dataInt(int size, long long v) override;
    void dataSym(const std::string &sym, long long off) override;
    void noteHasEh(bool yes) override;
    void initialiserEntry(const std::string &fn, bool dsoHandle) override;
    std::string labelText(const std::string &l) const override;
    void dataBytes(const std::string &bytes) override;

private:
    Spelling *under_ = nullptr;
    int level_ = 0;
    // The function so far, as chunks; run_ is the one the passes are working on.
    std::vector<IrChunk> chunks_;
    // Whether the last chunk still takes instructions - false after a terminator.
    bool open_ = false;
    bool alignNext_ = false;   // a loop head asked for: laid before the next label
    std::vector<IrIns> run_;
    unsigned long removed_ = 0;
    // True after a jmp or a ret with no label since: nothing reaches there.
    bool unreachable_ = false;
    // What is live, and whether the flags are, after the run being optimized.
    unsigned initLive_ = 0;
    bool initFlags_ = false;
    bool callReadsAccumulator_ = true;
    bool microsoftAbi_ = false;
    bool rdxLive_ = true;
    std::vector<std::pair<long long, int> > scalars_;
    // The prologue, held until the body is known so the saves can be named.
    bool prologueHeld_ = false;
    int heldFrame_ = 0;
    int heldOutgoing_ = 0;
    std::string heldLsda_;
    // Set by functionEnd: the flush that follows sees the whole body.
    bool ending_ = false;
    // The semantics, with the function's own facts applied.
    void semantics(IrIns &x) const;

    // Per-run scratch, kept as members so the storage is reused run to run.
    std::vector<unsigned> liveOut_;
    std::vector<unsigned char> flagsLiveOut_;
    std::vector<IrIns> kept_;

    void add(IrIns &&i);
    IrChunk &chunk();
    // A spelling call from inside a function, kept in its place.
    void event(std::function<void()> f);
    // Everything that cannot stay inside a function: writes it all out first.
    void interrupt();
    // Liveness over the chunks' graph, then each chunk optimized with what
    // really follows it, then all of it replayed in order.
    void connect();
    void flow();
    bool unroll();
    // Locals into callee-saved registers; the mask of those it used.
    unsigned promote();
    void placeRestore();
    void optimize();
    void replay();

    // The passes. Each works on run_ and marks what it removes dead;
    // compact() drops those, analyse() fills in the semantics and the liveness.
    void analyse();
    void compact();
    void peephole();
    bool dropExtensions();
    bool foldAddIntoAddress();
    bool fuseAddToLea();
    bool fuseLeas();
    bool sinkFrameLeas();
    bool pairStack();
    bool retargetDefs();
    bool renameThroughPair();
    bool foldCompareBranch();
    bool dropDeadDefs();
    bool moveSourceReads();
    bool propagateCopies();
    void shorten();
};
