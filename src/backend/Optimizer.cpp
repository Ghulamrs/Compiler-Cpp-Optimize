#include "Optimizer.h"

#include "Mir.h"
#include "OptPasses.h"

#include <algorithm>
#include <cassert>
#include <set>
#include <utility>

using opt::Entry;

Optimizer::Optimizer(Spelling &under, const Abi &abi, int level) : under_(under) {
    fn_.convention = opt::conventionOf(abi);
    fn_.costs = opt::Costs::forLevel(level);
}

void Optimizer::hold(Entry e) {
    fn_.stream.push_back(std::move(e));
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
    fn_.promotable = false;
    fn_.prologueAt = -1;
    fn_.inlineTop = 0;
    fn_.saves.clear();
    inlining_ = false;
}

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

// **A label only jumps name, that no jump names any more**, joins its block to
// the one before, so what is known flows through it.
void Optimizer::dropUnnamedLabels() {
    std::set<std::string> named;
    for (const Entry &e : fn_.stream)
        if (e.kind == Entry::Ins && !e.dead && e.ins.a.kind == opt::Operand::Label) named.insert(e.ins.a.text);
    for (Entry &e : fn_.stream)
        if (e.kind == Entry::Label && fn_.jumpOnly.count(e.label) && !named.count(e.label)) e.dead = true;
}

void Optimizer::functionEnd(const std::string &name) {
    flush();
    inFunction_ = false;
    under_.functionEnd(name);
}

void Optimizer::frame(std::vector<opt::Local> locals, bool promotable) {
    fn_.locals = std::move(locals);
    fn_.promotable = promotable;
}

void Optimizer::returnsPair(bool pair) {
    fn_.convention.returned = opt::bit(opt::RAX) | opt::bit(opt::kXmm0);
    if (pair) fn_.convention.returned |= opt::bit(opt::RDX) | opt::bit(opt::kXmm0 + 1);
}

void Optimizer::rounds(int limit) {
    opt::Stream &s = fn_.stream;
    opt::Flow &flow = fn_.flow;
    // A pass that changed something leaves the liveness to be solved again.
    bool changed = false;
    auto ran = [&](bool c) { if (c) { flow.touch(); changed = true; } };
    for (int round = 0; round < limit; ++round) {
        dropUnnamedLabels();
        fn_.buildFlow();
        changed = false;
        ran(opt::forwardValues(s, flow, fn_.convention));
        ran(opt::removeUnreachable(s));
        ran(opt::removeDead(s, flow));
        ran(opt::coalesceCopies(s, flow, fn_.convention));
        ran(opt::foldLoads(s, flow, fn_.convention));
        ran(opt::foldOffsets(s, flow, fn_.convention));
        if (!changed) break;
    }
}

// **Locals go to registers once the frame is as small as it gets**, so an
// address folded away no longer counts as escaping; then everything again.
void Optimizer::improve() {
    const opt::Costs &costs = fn_.costs;
    opt::Stream &s = fn_.stream;
    opt::Flow &flow = fn_.flow;
    rounds(costs.rounds);
    // What the frame gains goes below what it had: first the region inlined
    // callees live in, then the saves. The outgoing area stays under both.
    fn_.size = fn_.frameBase();
    if (fn_.whole && fn_.promotable && fn_.prologueAt >= 0) {
        // Stage 1 of docs/OPTIMIZER-IR.md: every web a pseudo, and each given
        // back the register it was found in - which must change nothing.
        const mir::Webs webs = mir::buildWebs(s, flow, fn_.convention);
        mir::assign(s, webs.home);
        fn_.saves = opt::promoteLocals(s, fn_.convention, fn_.locals, fn_.size, costs.registers, costs.minWeight);
        flow.touch();
        if (!fn_.saves.empty()) rounds(costs.rounds);
        // Each round can forward a reload away and leave its store unread.
        for (int again = 0; again < 3 && opt::removeDeadStores(s); ++again) { flow.touch(); rounds(costs.rounds); }
        opt::dropUnusedSaves(s, fn_.saves);
        flow.touch();
        fn_.size += (8 * static_cast<int>(fn_.saves.size()) + 15) & ~15;
    }
    if (fn_.size != fn_.frameSize) {
        assert(fn_.prologueAt >= 0 && "a frame can grow only while its prologue is held");
        const std::vector<SavedReg> saves = fn_.saves;
        const int size = fn_.size;
        const std::string lsda = fn_.lsda;
        const int outgoing = fn_.outgoing;
        s[fn_.prologueAt].event = [=](Spelling &sp) { sp.calleeSaves(saves); sp.prologue(size, lsda, outgoing); };
    }
    // A shorter spelling last: narrowed arithmetic leaves extensions to delete.
    for (int again = 0; again < 3; ++again) {
        fn_.buildFlow();
        if (!opt::shrink(s, flow, fn_.convention)) break;
        flow.touch();
        rounds(costs.rounds);
    }
}

void Optimizer::settle() {
    if (inFunction_) fn_.whole = false;
    flush();
}

void Optimizer::flush() {
    if (fn_.stream.empty()) return;
    improve();
    for (const Entry &e : fn_.stream) {
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
    fn_.stream.clear();
    fn_.prologueAt = -1;       // written out: from here on the frame is what it is
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
