#include "Optimizer.h"

#include "OptPipeline.h"

#include <algorithm>
#include <cassert>
#include <utility>

using opt::Entry;

Optimizer::Optimizer(Spelling &under, const Abi &abi, int level)
    : under_(under), costs_(opt::Costs::forLevel(level)), fn_(*costs_), pipeline_(opt::pipelineFor()) {
    fn_.convention = opt::conventionOf(abi);
}

void Optimizer::hold(Entry e) {
    current().stream.push_back(std::move(e));
    held_++;
}

void Optimizer::instruction(const std::string &m, int operands, const Op *a, const Op *b) {
    if (!inFunction_) {
        if (operands == 0) under_.ins(m);
        else if (operands == 1) under_.ins(m, *a);
        else under_.ins(m, *a, *b);
        return;
    }
    Entry e;
    e.ins.m = m;
    e.ins.operands = operands;
    if (a) e.ins.a = opt::Operand::from(*a);
    if (b) e.ins.b = opt::Operand::from(*b);
    if (argsPending_ && m == "call") { e.ins.args = args_; e.ins.exactArgs = true; }
    argsPending_ = false;
    if (inlining_)
        for (opt::Operand *o : {&e.ins.a, &e.ins.b})
            if (o->isMem() && o->reg.id == opt::RBP) {
                assert(o->disp < 0 && "an inlined callee reads only its own frame");
                o->disp -= inlineBase_;
                o->hasDisp = true;
            }
    hold(std::move(e));
}

void Optimizer::event(std::function<void(Spelling &)> call) {
    if (!inFunction_) { call(under_); return; }
    Entry e;
    e.kind = Entry::Event;
    e.event = std::move(call);
    hold(std::move(e));
}

void Optimizer::ins(const std::string &m) { instruction(m, 0, nullptr, nullptr); }
void Optimizer::ins(const std::string &m, const Op &a) { instruction(m, 1, &a, nullptr); }
void Optimizer::ins(const std::string &m, const Op &a, const Op &b) { instruction(m, 2, &a, &b); }

void Optimizer::defLabel(const std::string &l) {
    if (!inFunction_) { under_.defLabel(l); return; }
    Entry e;
    e.kind = Entry::Label;
    e.label = l;
    hold(std::move(e));
}

// **Held as a label with a mark**: whether a call ends on it is known only
// once the passes are done, and the spelling judges that from what it is given.
void Optimizer::stateLabel(const std::string &l) {
    if (!inFunction_) { under_.stateLabel(l); return; }
    defLabel(l);
    fn_.stream.back().state = true;
}

void Optimizer::functionBegin(const std::string &name, bool exported, bool mergeable) {
    flush();
    under_.functionBegin(name, exported, mergeable);
    inFunction_ = true;
    fn_.name = name;
    fn_.whole = true;
    fn_.prologueAt = -1;
    fn_.inlineTop = 0;
    fn_.saves.clear();
    fn_.shared = opt::SharedSlots();
    inlining_ = false;
}

// **A funclet is a function of its own to the passes** - the runtime calls
// it, with the parent's frame under rbp - and a piece, never whole: no pass
// takes a register for it that its prologue does not save. What it reads or
// writes of the parent's frame is shared with the parent, whose passes then
// leave those slots alone.
void Optimizer::funcletBegin() {
    assert(inFunction_ && !funclet_ && "a funclet inside a funclet");
    funclet_.reset(new opt::Function(*costs_));
    funclet_->name = fn_.name;
    funclet_->convention = fn_.convention;
    funclet_->whole = false;
}

void Optimizer::funcletEnd() {
    assert(funclet_ && "no funclet is held");
    improve(*funclet_);
    fn_.shared.addAccessesOf(funclet_->stream);
    funclets_.push_back(std::move(funclet_->stream));
    funclet_.reset();
}

void Optimizer::defer(std::function<void()> call) {
    event([call](Spelling &) { call(); });
}

void Optimizer::sharedSlot(long long disp, int size) { fn_.shared.add(disp, size); }

// **A callee walked in place keeps its frame below the caller's locals**, moved
// down by `base`. Every site shares that region, so no slot in it is one
// variable's, and none is offered for a register.
void Optimizer::inlineBegin(int base, int calleeFrame) {
    inlining_ = true;
    inlineBase_ = base;
    fn_.inlineTop = std::max(fn_.inlineTop, base + ((calleeFrame + 15) & ~15));
}

void Optimizer::inlineEnd() { inlining_ = false; }

void Optimizer::jumpOnly(const std::string &label) { fn_.jumpOnly.insert(label); }

void Optimizer::exceptionRegion(const std::string &begin, const std::string &end, const std::string &target) {
    fn_.regions.push_back(opt::Region{begin, end, target});
}

void Optimizer::functionEnd(const std::string &name) {
    flush();
    inFunction_ = false;
    under_.functionEnd(name);
}

void Optimizer::frame(std::vector<opt::Local> locals) { fn_.locals = std::move(locals); }

void Optimizer::callArguments(opt::RegSet regs) {
    argsPending_ = true;
    args_ = regs;
}

void Optimizer::returnsPair(bool pair) {
    fn_.convention.returned = opt::bit(opt::RAX) | opt::bit(opt::kXmm0);
    if (pair) fn_.convention.returned |= opt::bit(opt::RDX) | opt::bit(opt::kXmm0 + 1);
}

// **The passes, through the manager.** What ran here as hand-written loops
// is the pipeline in OptPipeline.cpp; the manager runs it, checks what each
// pass requires, and dumps after any pass CXX1_DUMP_MIR names.
void Optimizer::improve(opt::Function &fn) { manager_.run(*pipeline_, fn); }

// **The function, then its funclets**, the frame's final size having been
// written with the function's prologue - a funclet's operands are rendered
// against it.
void Optimizer::flush() {
    if (fn_.stream.empty()) return;
    improve(fn_);
    replay(fn_.stream);
    for (const opt::Stream &f : funclets_) replay(f);
    frameSize_ = fn_.size;
    funclets_.clear();
    fn_.stream.clear();
    fn_.regions.clear();       // written out with the labels they name
    fn_.props = opt::kPropPhysical;   // a new stream: the flow describes nothing yet
    fn_.prologueAt = -1;       // written out: from here on the frame is what it is
}

void Optimizer::replay(const opt::Stream &s) {
    for (const Entry &e : s) {
        if (e.dead) continue;
        switch (e.kind) {
        case Entry::Ins: {
            const opt::Instr &i = e.ins;
            if (i.operands == 0) under_.ins(i.m);
            else if (i.operands == 1) under_.ins(i.m, i.a.op());
            else under_.ins(i.m, i.a.op(), i.b.op());
            break;
        }
        case Entry::Label:
            if (e.state) under_.stateLabel(e.label);
            else under_.defLabel(e.label);
            break;
        case Entry::Event: e.event(under_); break;
        }
    }
}

// Everything else is an event: held where it stood, with its arguments copied.
void Optimizer::prologue(int frameSize, const std::string &lsda, int outgoing) {
    fn_.prologueAt = static_cast<int>(fn_.stream.size());
    fn_.frameSize = frameSize;
    fn_.lsda = lsda;
    fn_.outgoing = outgoing;
    event([=](Spelling &s) { s.prologue(frameSize, lsda, outgoing); });
}
void Optimizer::fileEntry(int n, const std::string &name) {
    event([=](Spelling &s) { s.fileEntry(n, name); });
}
void Optimizer::location(int file, int line, int column) {
    event([=](Spelling &s) { s.location(file, line, column); });
}
void Optimizer::globl(const std::string &name) { event([=](Spelling &s) { s.globl(name); }); }
void Optimizer::weakDefinition(const std::string &name) {
    event([=](Spelling &s) { s.weakDefinition(name); });
}
void Optimizer::textSection() { event([](Spelling &s) { s.textSection(); }); }
void Optimizer::rodataSection() { event([](Spelling &s) { s.rodataSection(); }); }
void Optimizer::dataSection() { event([](Spelling &s) { s.dataSection(); }); }
void Optimizer::bssSection() { event([](Spelling &s) { s.bssSection(); }); }
void Optimizer::objectType(const std::string &name) {
    event([=](Spelling &s) { s.objectType(name); });
}
void Optimizer::objectSize(const std::string &name, int size) {
    event([=](Spelling &s) { s.objectSize(name, size); });
}
void Optimizer::align(int n) { event([=](Spelling &s) { s.align(n); }); }
void Optimizer::zero(int n) { event([=](Spelling &s) { s.zero(n); }); }
void Optimizer::dataInt(int size, long long v) { event([=](Spelling &s) { s.dataInt(size, v); }); }
void Optimizer::dataSym(const std::string &sym, long long off) {
    event([=](Spelling &s) { s.dataSym(sym, off); });
}
void Optimizer::noteHasEh(bool yes) { event([=](Spelling &s) { s.noteHasEh(yes); }); }
void Optimizer::initialiserEntry(const std::string &fn, bool dsoHandle) {
    event([=](Spelling &s) { s.initialiserEntry(fn, dsoHandle); });
}
void Optimizer::dataBytes(const std::string &bytes) {
    event([=](Spelling &s) { s.dataBytes(bytes); });
}
