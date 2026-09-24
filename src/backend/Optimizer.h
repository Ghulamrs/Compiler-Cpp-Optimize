#pragma once

// **-O1 and -O2, as a spelling in front of the spelling.** The walker writes a
// function as it always has; this holds it, improves it and hands it on.
// Outside a function every call passes straight through.

#include "OptFunction.h"
#include "OptPass.h"
#include "Spelling.h"

#include <cstddef>
#include <memory>

struct Abi;

class Optimizer final : public Spelling {
public:
    Optimizer(Spelling &under, const Abi &abi, int level);

    // **Write out what is held**, before the walker cuts a funclet out of its text.
    void settle();
    // Grows with every entry held, never shrinks: Walker's measure of held code.
    std::size_t held() const { return held_; }
    // Whether the function being held returns in two registers, rax:rdx or xmm0:xmm1.
    void returnsPair(bool pair);
    // The function's scalar locals, and whether it may keep any in registers.
    void frame(std::vector<opt::Local> locals, bool promotable);
    // Around a callee walked in place of its call, at -O2.
    void inlineBegin(int base, int calleeFrame);
    void inlineEnd();
    // A label the walker names only in jumps, which may go once nothing jumps to it.
    void jumpOnly(const std::string &label);
    int level() const { return costs_->level(); }
    // Whether this level copies a block with `rep movsq` rather than unrolled.
    bool copiesByString() const { return costs_->stringCopies(); }

    void ins(const std::string &m) override;
    void ins(const std::string &m, const Op &a) override;
    void ins(const std::string &m, const Op &a, const Op &b) override;
    void defLabel(const std::string &l) override;
    void stateLabel(const std::string &l) override;

    void functionBegin(const std::string &name, bool exported, bool mergeable) override;
    void prologue(int frameSize, const std::string &lsda, int outgoing) override;
    void functionEnd(const std::string &name) override;

    void fileEntry(int n, const std::string &name) override;
    void location(int file, int line, int column) override;
    void predefine(const std::vector<std::string> &names) override { under_.predefine(names); }
    void preamble(std::ostream &o) override { under_.preamble(o); }
    void postamble(std::ostream &o) override { under_.postamble(o); }

    void globl(const std::string &name) override;
    void weakDefinition(const std::string &name) override;
    void textSection() override;
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
    std::string labelText(const std::string &l) const override { return under_.labelText(l); }
    void dataBytes(const std::string &bytes) override;

private:
    Spelling &under_;
    // The level, as the costs its passes ask.
    std::unique_ptr<const opt::Costs> costs_;
    // **The function being held**, and what the walker has said of it so far.
    opt::Function fn_;
    // The pipeline, built once per spelling, and the manager that runs it.
    std::unique_ptr<opt::Pass> pipeline_;
    opt::PassManager manager_;
    bool inFunction_ = false;
    std::size_t held_ = 0;
    bool inlining_ = false;
    int inlineBase_ = 0;

    void hold(opt::Entry e);
    void instruction(const std::string &m, int operands, const Op *a, const Op *b);
    // A call that is not an instruction: held in its place inside a function,
    // passed on at once outside one.
    void event(std::function<void(Spelling &)> call);
    void improve();
    void flush();
};
