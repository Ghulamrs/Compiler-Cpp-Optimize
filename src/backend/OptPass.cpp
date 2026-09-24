#include "OptPass.h"

#include "Mir.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <set>
#include <sstream>

namespace opt {

PassManager::PassManager() {
    const char *want = std::getenv("CXX1_DUMP_MIR");
    if (!want) return;
    std::stringstream in(want);
    std::string name;
    while (std::getline(in, name, ',')) {
        if (name == "all") dumpAll_ = true;
        else if (!name.empty()) dump_.push_back(name);
    }
}

bool PassManager::wanted(const Pass &pass) const {
    if (dumpAll_) return true;
    for (const std::string &n : dump_) if (n == pass.info().name) return true;
    return false;
}

bool PassManager::run(Pass &pass, Function &fn) {
    if (!pass.gate(fn)) return false;
    if (Group *g = dynamic_cast<Group *>(&pass)) return runGroup(*g, fn);
    return runLeaf(pass, fn);
}

void PassManager::before(const Pass &pass, Function &fn) {
    const PassInfo &info = pass.info();
    if (info.todoStart & kTodoDropLabels) dropUnnamedLabels(fn);
    if (info.todoStart & kTodoBuildFlow) fn.buildFlow();
    assert(fn.has(info.required) && "a pass ran without a property it requires");
}

// The properties as GCC updates them, one line; a change leaves the
// liveness to be solved again before anything reads it.
void PassManager::after(const Pass &pass, Function &fn, bool changed) {
    const PassInfo &info = pass.info();
    fn.props = (fn.props | info.provided) & ~info.destroyed;
    if (changed) fn.flow.touch();
    if (wanted(pass)) {
        std::cerr << ";; " << fn.name << " after " << info.name << (changed ? " (changed)" : "") << "\n";
        dumpStream(std::cerr, fn);
    }
}

bool PassManager::runLeaf(Pass &pass, Function &fn) {
    before(pass, fn);
    const bool changed = pass.execute(fn);
    after(pass, fn, changed);
    return changed;
}

bool PassManager::runGroup(Group &group, Function &fn) {
    bool any = false;
    const int limit = group.repeat(fn);
    for (int round = 0; round < limit; ++round) {
        before(group, fn);
        bool changed = false;
        bool first = true;
        for (const std::unique_ptr<Pass> &sub : group.subs()) {
            const bool c = run(*sub, fn);
            changed = changed || c;
            if (first && group.stop() == Group::WhenFirstUnchanged && !c) break;
            first = false;
        }
        any = any || changed;
        after(group, fn, changed);
        if (!changed) break;
    }
    return any;
}

void dropUnnamedLabels(Function &fn) {
    std::set<std::string> named;
    for (const Entry &e : fn.stream)
        if (e.kind == Entry::Ins && !e.dead && e.ins.a.kind == Operand::Label) named.insert(e.ins.a.text);
    for (Entry &e : fn.stream)
        if (e.kind == Entry::Label && fn.jumpOnly.count(e.label) && !named.count(e.label)) e.dead = true;
}

namespace {

std::string regText(const Reg &r, const std::string &text) {
    if (r.id < 0) return text;
    if (mir::isPseudo(r.id)) return "%p" + std::to_string(r.id - mir::kFirstPseudo) + ":" + std::to_string(r.width);
    return regName(r.id, r.width);
}

std::string operandText(const Operand &o) {
    switch (o.kind) {
    case Operand::None: return "";
    case Operand::Register: return regText(o.reg, o.text);
    case Operand::Immediate: return "$" + (o.numeric ? std::to_string(o.value) : o.text);
    case Operand::Memory:
        return (o.hasDisp ? std::to_string(o.disp) : std::string()) + "(" +
               (o.reg.id >= 0 ? regText(Reg{o.reg.id, 8}, o.text) : o.text) + ")";
    case Operand::RipSymbol: return o.text + "(%rip)";
    case Operand::Indirect: return "*" + regText(o.reg, o.text);
    case Operand::Label: return o.text;
    }
    return "";
}

}

// **The stream, one entry a line**: its index, a `-` for a dead entry, the
// instruction, `label:` or `<event>`; a block boundary where the flow has
// one, with what is live in and out of it when that has been solved.
void dumpStream(std::ostream &o, const Function &fn) {
    std::vector<int> blockAt(fn.stream.size() + 1, -1);
    if (fn.has(kPropFlow))
        for (std::size_t b = 0; b < fn.flow.blocks.size(); ++b) blockAt[fn.flow.blocks[b].begin] = static_cast<int>(b);
    for (std::size_t k = 0; k < fn.stream.size(); ++k) {
        if (blockAt[k] >= 0) {
            const Block &blk = fn.flow.blocks[blockAt[k]];
            o << ";;   block " << blockAt[k] << " [" << blk.begin << ", " << blk.end << ")";
            if (!fn.flow.solutionsDirty) o << " live in " << std::hex << blk.liveIn << " out " << blk.liveOut << std::dec;
            o << "\n";
        }
        const Entry &e = fn.stream[k];
        o << (e.dead ? "  -" : "   ") << k << "\t";
        switch (e.kind) {
        case Entry::Ins:
            o << e.ins.m;
            if (e.ins.operands >= 1) o << " " << operandText(e.ins.a);
            if (e.ins.operands >= 2) o << ", " << operandText(e.ins.b);
            break;
        case Entry::Label: o << e.label << ":" << (e.state ? "  ; state" : ""); break;
        case Entry::Event: o << "<event>"; break;
        }
        o << "\n";
    }
}

}
