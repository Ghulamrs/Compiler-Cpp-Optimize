#pragma once

#include <cassert>
#include <cstddef>
#include <cstring>
#include <iosfwd>
#include <string>
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
    // **A label where a Microsoft frame's exception state changes.** The
    // runtime reads the state at a return address, so a call must not end
    // exactly on one; the spellings that serve that runtime pad it (cl's npad 1).
    virtual void stateLabel(const std::string &l) { defLabel(l); }

    virtual void functionBegin(const std::string &name, bool exported,
                               bool mergeable = false) = 0;
// `lsda` names this function's exception table, or is empty where it has no
// landing pad. The personality routine must be named between .cfi_startproc
// and the first instruction, and this is the only place that sees both.
// `outgoing` is the area at the very bottom of the frame that a call made at
// the frame's floor finds its shadow space in - below the locals, below the
// callee-saved spills, below anything an optimizer adds to the frame.
    virtual void prologue(int frameSize, const std::string &lsda, int outgoing) = 0;
    virtual void functionEnd(const std::string &name) = 0;
    // Said before the prologue, which saves them where the unwinder looks.
    virtual void calleeSaves(const std::vector<SavedReg> &saves) { assert(saves.empty()); (void)saves; }

    virtual void fileEntry(int, const std::string &) {}
    virtual void location(int, int, int) {}

    virtual void predefine(const std::vector<std::string> &) {}
    virtual void preamble(std::ostream &) {}
    virtual void postamble(std::ostream &) {}

    virtual void globl(const std::string &name) = 0;
    // **A definition the linker must fold rather than reject** - an inline
    // function, which may appear in several translation units.
    virtual void weakDefinition(const std::string &name) { (void)name; }
    virtual void textSection() = 0;
    virtual void rodataSection() = 0;
    virtual void dataSection() = 0;
    virtual void bssSection() = 0;
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
    virtual void dataBytes(const std::string &bytes) = 0;
};

// **The GNU spelling, and the one place a COFF variant needs to differ.**
// `sym` is what every name goes through on its way out.
class GnuSpelling : public Spelling {
public:
    explicit GnuSpelling(std::string &o) : o_(o) {}
    virtual ~GnuSpelling() {}

    void ins(const std::string &m) override;
    void ins(const std::string &m, const Op &a) override;
    void ins(const std::string &m, const Op &a, const Op &b) override;

    void defLabel(const std::string &l) override;
    void stateLabel(const std::string &l) override;
    void fileEntry(int n, const std::string &name) override;
    void location(int file, int line, int column) override;
    void functionBegin(const std::string &name, bool exported,
                       bool mergeable = false) override;
    void prologue(int frameSize, const std::string &lsda, int outgoing) override;
    void functionEnd(const std::string &name) override;
    void calleeSaves(const std::vector<SavedReg> &saves) override { saves_ = saves; }
    void globl(const std::string &name) override;
    void weakDefinition(const std::string &name) override;
    void textSection() override;
    void rodataSection() override;
    void dataSection() override;
    void bssSection() override;
    void objectType(const std::string &name) override;
    void objectSize(const std::string &name, int size) override;
    void align(int n) override;
    void loopAlign(int bytes) override;
    void zero(int n) override;
    void dataInt(int size, long long v) override;
    void dataSym(const std::string &s, long long off) override;
    void initialiserEntry(const std::string &fn, bool dsoHandle) override;

protected:
    // How a symbol is written. Identity for GNU-as on ELF and Mach-O.
    virtual std::string sym(const std::string &name) const { return name; }
    virtual void op(const Op &x);
    void dataBytes(const std::string &bytes) override;

protected:
    std::string &o_;
    std::vector<SavedReg> saves_;
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
    void rodataSection() override;
    void dataSection() override;
    void bssSection() override;
    void align(int n) override;
    void objectType(const std::string &name) override;
    void objectSize(const std::string &name, int size) override;
    void prologue(int frameSize, const std::string &lsda, int outgoing) override;
    void functionEnd(const std::string &name) override;
    void noteHasEh(bool yes) override { hasEh_ = yes; }
    void initialiserEntry(const std::string &fn, bool dsoHandle) override;

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
};
