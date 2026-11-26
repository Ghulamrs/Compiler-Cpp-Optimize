#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>
#include <iosfwd>
#include <set>
#include <string>
#include <utility>
#include <vector>

struct Str {
    const char *p = "";
    std::size_t n = 0;

    Str() = default;

    Str(const char *s) : p(s), n(0) {
        assert(s != nullptr);
        n = std::strlen(s);
    }
    Str(const std::string &s) : p(s.data()), n(s.size()) {}
    Str(const char *s, std::size_t len) : p(s), n(len) {}

    Str substr(std::size_t from) const {
        assert(from <= n);
        return Str(p + from, n - from);
    }
    bool startsWith(const char *s) const {
        std::size_t m = std::strlen(s);
        return n >= m && std::memcmp(p, s, m) == 0;
    }

    explicit operator std::string() const { return std::string(p, n); }
};

inline std::string &operator+=(std::string &s, Str v) {
    return s.append(v.p, v.n);
}

struct Op {
    enum Kind {
        Reg,
        Imm,
        Mem,
        Rip,
        Ind,
        Lbl,
        // **Thread storage**: a word at a fixed offset off a segment register - `%fs:0`, the thread
        // pointer - and a thread_local's symbol as a displacement off a base register, which each
        // spelling relocates its own way: `x@TPOFF(%rax)`, `"x"@SECREL32(%rax)`, `[rax + SECTIONREL x]`.
        Seg,
        Tls,
    };
    Kind kind;

    Str text;
    long long disp = 0;
    bool hasDisp = false;

    unsigned long long uimm = 0;
    bool immNeg = false;
    bool immNumeric = false;

    // Mem only: `d(base,index,scale)`; scale 0 is no index.
    Str index;
    int scale = 0;
};

inline Op reg(Str r)  { return { Op::Reg, r, 0, false, 0, false, false, {}, 0 }; }
inline Op imm(long long v) {
    Op o { Op::Imm, {}, 0, false, 0, false, true, {}, 0 };
    if (v < 0) { o.immNeg = true; o.uimm = 0ULL - static_cast<unsigned long long>(v); }
    else o.uimm = static_cast<unsigned long long>(v);
    return o;
}
inline Op imm(unsigned long long v) { return { Op::Imm, {}, 0, false, v, false, true, {}, 0 }; }
inline Op imm(int v)                { return imm(static_cast<long long>(v)); }
inline Op imm(unsigned int v)       { return imm(static_cast<unsigned long long>(v)); }

inline Op immText(Str t) { return { Op::Imm, t, 0, false, 0, false, false, {}, 0 }; }

inline Op mem(Str base) { return { Op::Mem, base, 0, false, 0, false, false, {}, 0 }; }
inline Op mem(long long d, Str base)
                                   { return { Op::Mem, base, d, true, 0, false, false, {}, 0 }; }
inline Op memIndexed(long long d, bool hasD, Str base, Str index, int scale) {
    return { Op::Mem, base, d, hasD, 0, false, false, index, scale };
}
inline Op rip(Str sym) { return { Op::Rip, sym, 0, false, 0, false, false, {}, 0 }; }
inline Op ind(Str r)   { return { Op::Ind, r, 0, false, 0, false, false, {}, 0 }; }
inline Op lbl(Str l)   { return { Op::Lbl, l, 0, false, 0, false, false, {}, 0 }; }
inline Op seg(Str r, long long d) { return { Op::Seg, r, d, true, 0, false, false, {}, 0 }; }
inline Op tls(Str symbol, Str base) { return { Op::Tls, symbol, 0, false, 0, false, false, base, 0 }; }

inline void appendNum(std::string &s, unsigned long long v) {
    char b[24];
    int i = 24;
    if (v == 0) b[--i] = '0';
    while (v != 0) { b[--i] = static_cast<char>('0' + v % 10); v /= 10; }
    s.append(b + i, static_cast<std::size_t>(24 - i));
}
inline void appendNum(std::string &s, long long v) {
    if (v < 0) { s += '-'; appendNum(s, 0ULL - static_cast<unsigned long long>(v)); }
    else appendNum(s, static_cast<unsigned long long>(v));
}
inline void appendNum(std::string &s, int v) { appendNum(s, static_cast<long long>(v)); }

// **A callee-saved register the optimizer keeps a local in**, and the frame
// slot the prologue saves it to - rbp-relative, as the walker addresses a local.
struct SavedReg {
    std::string reg;
    long long disp;
};

class Spelling {
public:
    virtual ~Spelling() = default;

    virtual void ins(const std::string &m) = 0;
    virtual void ins(const std::string &m, const Op &a) = 0;
    virtual void ins(const std::string &m, const Op &a, const Op &b) = 0;

    virtual void defLabel(const std::string &l) = 0;
    // **A label where a Microsoft frame's exception state changes.** The runtime reads the state at a return address, so a call must not end exactly on one; the spellings that serve that runtime pad it (cl's npad 1).
    virtual void stateLabel(const std::string &l) { defLabel(l); }
    // **A lexical block's begin or end under -g**, which debug records name and nothing jumps to; the optimizer holds it
    // as an event, so that it changes no code.
    virtual void blockLabel(const std::string &l) { defLabel(l); }

    virtual void functionBegin(const std::string &name, bool exported,
                               bool mergeable = false) = 0;
// `lsda` names this function's exception table, or is empty where it has no landing pad; the personality routine must be named between .cfi_startproc and the first instruction, and this is the only place that sees both.
// `outgoing` is the area at the very bottom of the frame that a call made at the frame's floor finds its shadow space in - below the locals, below the callee-saved spills, below anything an optimizer adds to the frame.
// `realign` past 0 is the alignment a local asked for beyond the stack's: rbx is saved, rsp rounded down to it after the allocation, and the floor left in rbx, which every slot is addressed from (X86_64Linux::local) - clang's `andq $-64, %rsp`.
    virtual void prologue(int frameSize, const std::string &lsda, int outgoing, int realign = 0) = 0;
    virtual void functionEnd(const std::string &name) = 0;
    // Said before the prologue, which saves them where the unwinder looks.
    virtual void calleeSaves(const std::vector<SavedReg> &saves) { assert(saves.empty()); (void)saves; }

    virtual void fileEntry(int, const std::string &) {}
    virtual void location(int, int, int) {}
    // **-g inside a handler funclet**: its code is in another section, so its
    // lines are a table of their own between these two; `locationDirective` is
    // one location of function `fn`, for a funclet's entry.
    virtual void funcletLinesBegin() {}
    virtual void funcletLinesEnd() {}
    virtual std::string locationDirective(int fn, int file, int line, int column) const {
        (void)fn; (void)file; (void)line; (void)column; return std::string();
    }

    // **atomic_flag's exchange** - [atomics.flag]/2 wants it lock-free, and a language with no interlocked operation
    // of its own has it from here: a function private to the file, `bool __cxx1_flag_exchange(volatile bool *, bool)`,
    // which to the optimizer is a call like any other.
    virtual void flagExchange() {}
    // **<atomic>'s interlocked operations** ([atomics.lockfree]; 1.75): `__cxx1_atomic_<op>_<bytes>`, op one of load,
    // xchg, add and cas, bytes 1, 2, 4 or 8, each a function private to the file as flagExchange's is -
    //   uN   load(const volatile void *)            the object's value
    //   uN   xchg(volatile void *, uN)              the old value; a store is one whose answer is dropped
    //   uN   add (volatile void *, uN)              the old value
    //   bool cas (volatile void *, uN *e, uN d)     d stored where the object held *e, else *e takes what it holds
    // - written with the processor's locked instructions (x86-64) or exclusive pairs (arm64), so that an atomic
    // scalar is lock-free there. The header asks for none on a target whose spelling writes none.
    virtual void atomicHelper(const std::string &name) { (void)name; }
    struct AtomicHelper {
        enum Op { None, Load, Xchg, Add, Cas } op;
        int k;      // 0, 1, 2, 3 for 1, 2, 4, 8 bytes
    };
    static AtomicHelper atomicHelperOf(const std::string &name) {
        AtomicHelper h = { AtomicHelper::None, 0 };
        static const char prefix[] = "__cxx1_atomic_";
        const std::size_t n = sizeof prefix - 1;
        if (name.compare(0, n, prefix) != 0 || name.size() < n + 5 || name[name.size() - 2] != '_') return h;
        const char size = name[name.size() - 1];
        if (size == '1') h.k = 0; else if (size == '2') h.k = 1; else if (size == '4') h.k = 2; else if (size == '8') h.k = 3;
        else return h;
        const std::string op = name.substr(n, name.size() - n - 2);
        h.op = op == "load" ? AtomicHelper::Load : op == "xchg" ? AtomicHelper::Xchg : op == "add" ? AtomicHelper::Add
             : op == "cas" ? AtomicHelper::Cas : AtomicHelper::None;
        return h;
    }

    virtual void predefine(const std::vector<std::string> &) {}
    // Every chunk of the file's text, written and in order, before the preamble: what only the whole
    // file decides is spelled here (1.30: MASM's `div` instruction where `div` is a name).
    virtual void finishText(std::vector<std::string> &) {}
    virtual void preamble(std::ostream &) {}
    virtual void postamble(std::ostream &) {}

    virtual void globl(const std::string &name) = 0;
    // **A definition the linker must fold rather than reject** - an inline
    // function, which may appear in several translation units.
    virtual void weakDefinition(const std::string &name) { (void)name; }
    // Whether weakDefinition binds the symbol global by itself (ELF's `.weak`), so no globl goes before it.
    virtual bool weakBindsGlobal() const { return false; }
    virtual void textSection() = 0;
    virtual void rodataSection() = 0;
    // A constant holding addresses, which the loader writes before making it read-only; gcc's .data.rel.ro on ELF.
    virtual void relroSection() { rodataSection(); }
    virtual bool hasRelro() const { return false; }
    virtual void dataSection() = 0;
    virtual void bssSection() = 0;
    // The thread-local template: `.tdata`/`.tbss` on ELF, `.tls$` on COFF, where one copy per thread is made from.
    virtual void tlsDataSection() = 0;
    virtual void tlsBssSection() = 0;
    // **Where __tls_init is registered on COFF** - .CRT$XDU, which the CRT runs in every new thread - and the
    // directive that pulls `__dyn_tls_init` into the link; nothing on ELF, where the `_ZTH` aliases reach it.
    virtual void tlsInitialiserEntry(const std::string &fn) { (void)fn; }
    // Whether `[base + SECTIONREL x]` can be spelled; the project's masm writes SECTIONREL in a DD only, so a
    // thread_local's offset is read from a word holding it instead, `$tlsoff$x`, which this records (Phase 6).
    virtual bool tlsOperandsSpelled() const { return true; }
    virtual void tlsOffsetWord(const std::string &symbol) { (void)symbol; }
    virtual void objectType(const std::string &name) = 0;
    virtual void objectSize(const std::string &name, int size) = 0;
    virtual void align(int n) = 0;
    // A loop head, with the loop's estimated bytes: the pad that keeps it inside one line (OptAlign.cpp).
    virtual void loopAlign(int bytes) { (void)bytes; }
    virtual void zero(int n) = 0;
    virtual void dataInt(int size, long long v) = 0;

    virtual void dataSym(const std::string &sym, long long off) = 0;

    // **Whether a FuncInfo will actually be written beside this function.**
    virtual void noteHasEh(bool yes) { (void)yes; }

    // **Where the init function is registered** so the runtime calls it before main.
    virtual void initialiserEntry(const std::string &fn, bool dsoHandle) = 0;

    // How a label is written where a *table* names it, rather than a jump.
    // Identity everywhere but COFF, where a temporary cannot carry a
    // relocation - see CoffSpelling::sym.
    virtual std::string labelText(const std::string &l) const { return l; }
    // A label the spelling itself writes round a function's code - `$LNprolog$`, `$LNend$` - as a
    // table names it: the prefix and the symbol as one label here, the prefix before the symbol as
    // the spelling writes that in MASM (1.30, 2026-10-23: `_GLOBAL__sub_I_f.cpp` is `$_GLOBAL__...`).
    virtual std::string codeLabelText(const std::string &prefix, const std::string &symbol) {
        return labelText(prefix + symbol);
    }
    virtual void dataBytes(const std::string &bytes) = 0;
};

// **The GNU spelling, and the one place a COFF variant needs to differ.**
// `sym` is what every name goes through on its way out.
class GnuSpelling : public Spelling {
public:
    explicit GnuSpelling(std::string &o) : o_(o) {}
    virtual ~GnuSpelling() {}
    // The section the function last begun lives in - see textSection_.
    const std::string &currentText() const { return textSection_; }

    void ins(const std::string &m) override;
    void ins(const std::string &m, const Op &a) override;
    void ins(const std::string &m, const Op &a, const Op &b) override;

    void defLabel(const std::string &l) override;
    void stateLabel(const std::string &l) override;
    void fileEntry(int n, const std::string &name) override;
    void location(int file, int line, int column) override;
    void functionBegin(const std::string &name, bool exported,
                       bool mergeable = false) override;
    void prologue(int frameSize, const std::string &lsda, int outgoing, int realign) override;
    void functionEnd(const std::string &name) override;
    void calleeSaves(const std::vector<SavedReg> &saves) override { saves_ = saves; }
    void globl(const std::string &name) override;
    void weakDefinition(const std::string &name) override;
    bool weakBindsGlobal() const override { return true; }
    void textSection() override;
    void rodataSection() override;
    void relroSection() override;
    bool hasRelro() const override { return true; }
    void dataSection() override;
    void bssSection() override;
    void tlsDataSection() override;
    void tlsBssSection() override;
    void objectType(const std::string &name) override;
    void objectSize(const std::string &name, int size) override;
    void align(int n) override;
    void loopAlign(int bytes) override;
    void zero(int n) override;
    void dataInt(int size, long long v) override;
    void dataSym(const std::string &s, long long off) override;
    void initialiserEntry(const std::string &fn, bool dsoHandle) override;
    void flagExchange() override;
    void atomicHelper(const std::string &name) override;

protected:
    // How a symbol is written. Identity for GNU-as on ELF and Mach-O.
    virtual std::string sym(const std::string &name) const { return name; }
    virtual void op(const Op &x);
    void dataBytes(const std::string &bytes) override;

protected:
    std::string &o_;
    std::vector<SavedReg> saves_;
    // The section the function lives in: .text, or its own COMDAT group when mergeable.
    std::string textSection_ = "  .text\n";
    // Whether the last instruction spelled was a call - see stateLabel.
    bool afterCall_ = false;
};

// **The same GNU syntax, assembled into a COFF object.**
class CoffSpelling final : public GnuSpelling {
public:
    explicit CoffSpelling(std::string &o) : GnuSpelling(o) {}

    void functionBegin(const std::string &name, bool exported,
                       bool mergeable) override;
    void weakDefinition(const std::string &name) override;
    bool weakBindsGlobal() const override { return false; }
    void rodataSection() override;
    void relroSection() override { rodataSection(); }
    bool hasRelro() const override { return false; }
    void dataSection() override;
    void bssSection() override;
    void tlsDataSection() override;
    void tlsBssSection() override { tlsDataSection(); }
    void align(int n) override;
    void objectType(const std::string &name) override;
    void objectSize(const std::string &name, int size) override;
    void prologue(int frameSize, const std::string &lsda, int outgoing, int realign) override;
    void functionEnd(const std::string &name) override;
    void noteHasEh(bool yes) override { hasEh_ = yes; }
    void initialiserEntry(const std::string &fn, bool dsoHandle) override;
    void flagExchange() override;
    void atomicHelper(const std::string &name) override;
    void tlsInitialiserEntry(const std::string &fn) override;
    // CodeView's line entries in place of DWARF's - see CodeView.h.
    void fileEntry(int n, const std::string &name) override;
    void location(int file, int line, int column) override;
    void funcletLinesBegin() override;
    void funcletLinesEnd() override;
    std::string locationDirective(int fn, int file, int line, int column) const override;

    std::string labelText(const std::string &l) const override { return sym(l); }

protected:
    std::string sym(const std::string &name) const override;
    void op(const Op &x) override;

private:
    // What functionBegin has already given a COMDAT section, so the weakDefinition that follows it does not open a second one.
    std::string opened_;
    // The function being written, whether it needs a FuncInfo beside it, and
    // how far the frame pointer sits below the Itanium one - see op().
    std::string fnName_;
    bool hasEh_ = false;
    int frameSize_ = 0;
    // Whether this definition went into a COMDAT, which decides where its unwind data goes.
    bool mergeable_ = false;
    // **A data COMDAT is one object's section, and the next object must not
    // inherit it.** weakDefinition opens `.section .rdata,"dr",discard,"X"`
    // and nothing closed it, so the global emitted after X.
    const char *plainSection_ = "  .section .rdata,\"dr\"\n";
    int comdatData_ = 0;   // 0 none, 1 opened and awaiting its own align, 2 open
    // The unwind codes, built in the prologue and written at the end.
    std::string unwindData_;
    int unwindCodes_ = 0;
    // Whether a `.cv_file` was written (so -g), how many `.cv_func_id`s have been given out (functions and
    // funclets, in order), and the one a `.cv_loc` names now: the function's, or the funclet's inside one.
    bool codeView_ = false;
    int cvFunctions_ = 0;
    int cvCurrent_ = -1;
    // The last `.cv_loc` and where it was written, so one that no instruction follows can be replaced.
    std::string lastLoc_;
    std::size_t lastLocAt_ = 0;
    int lastLocFn_ = -1, lastLocFile_ = 0, lastLocLine_ = 0;
    // Every (file, line) the function being written has had a run of, so none gets a second.
    std::set<std::pair<int, int> > linesRun_;
    // The function's line state, set aside while a funclet's lines are written (funcletLinesBegin).
    struct LineState {
        int current; std::string lastLoc; std::size_t lastLocAt; int lastLocFn, lastLocFile, lastLocLine;
        std::set<std::pair<int, int> > linesRun;
    };
    std::vector<LineState> outerLines_;
};
