#include "OptPasses.h"
#include "OptFunction.h"

#include <algorithm>
#include <map>
#include <set>

namespace opt {

namespace {

bool gpr(const Operand &o) { return o.kind == Operand::Register && o.reg.id >= 0 && o.reg.id < kGprs; }
bool isJump(const Instr &i) { return i.a.kind == Operand::Label && (i.m == "jmp" || !conditionOf(i.m).empty()); }

// The first block each label heads.
std::map<std::string, int> labelBlocks(const Stream &s, const Flow &f) {
    std::map<std::string, int> out;
    for (int b = 0; b < static_cast<int>(f.blocks.size()); ++b)
        for (int k = f.blocks[b].begin; k < f.blocks[b].end && s[k].kind != Entry::Ins; ++k)
            if (s[k].kind == Entry::Label && !s[k].dead && !out.count(s[k].label)) out[s[k].label] = b;
    return out;
}

// The live instructions of a block, in order; empty if an event sits among them.
std::vector<int> liveInstrs(const Stream &s, const Block &blk) {
    std::vector<int> out;
    for (int k = blk.begin; k < blk.end; ++k) {
        if (s[k].dead) continue;
        if (s[k].kind == Entry::Event) return {};
        if (s[k].kind == Entry::Ins) out.push_back(k);
    }
    return out;
}

// **What a register holds at the end of a block, from the block alone**: the
// last write is `mov $v` at four or eight bytes, or a zeroing xor.
bool constantAtEnd(const Stream &s, const Flow &f, const Block &blk, int r, long long &value) {
    for (int k = blk.end - 1; k >= blk.begin; --k) {
        if (s[k].kind != Entry::Ins || s[k].dead) continue;
        const Instr &i = s[k].ins;
        const Effects &e = f.effects[k];
        if (!((e.writes | e.partial) & bit(r))) continue;
        if (i.operands != 2 || !gpr(i.b) || i.b.reg.id != r || i.b.reg.width < 4) return false;
        if (isMovQL(i.m) && i.a.kind == Operand::Immediate && i.a.numeric) {
            value = i.b.reg.width == 4 ? static_cast<long long>(static_cast<unsigned>(i.a.value)) : i.a.value;
            return true;
        }
        if (i.m == "xor" && i.a.isReg(r)) { value = 0; return true; }
        return false;
    }
    return false;
}

// Whether `jcc` is taken for a compare of a known register against an immediate, or a test of it.
bool decides(const Instr &cmp, const std::string &cc, long long r64, bool &taken) {
    const bool test = cmp.m == "test";
    if (test) { if (!(gpr(cmp.a) && gpr(cmp.b) && cmp.a.reg.id == cmp.b.reg.id)) return false; }
    else if (!((cmp.m == "cmp" || cmp.m == "cmpl" || cmp.m == "cmpq") && cmp.a.kind == Operand::Immediate &&
               cmp.a.numeric && gpr(cmp.b)))
        return false;
    const int w = cmp.b.reg.width;
    if (w != 4 && w != 8) return false;
    const long long lhs = w == 4 ? static_cast<int>(static_cast<unsigned>(r64)) : r64;
    const long long rhs = test ? 0 : (w == 4 ? static_cast<int>(static_cast<unsigned>(cmp.a.value)) : cmp.a.value);
    const unsigned long long ul = w == 4 ? static_cast<unsigned>(lhs) : static_cast<unsigned long long>(lhs);
    const unsigned long long ur = w == 4 ? static_cast<unsigned>(rhs) : static_cast<unsigned long long>(rhs);
    if (cc == "e" || cc == "z") taken = lhs == rhs;
    else if (cc == "ne" || cc == "nz") taken = lhs != rhs;
    else if (cc == "l") taken = lhs < rhs;
    else if (cc == "le") taken = lhs <= rhs;
    else if (cc == "g") taken = lhs > rhs;
    else if (cc == "ge") taken = lhs >= rhs;
    else if (test && cc == "s") taken = lhs < 0;
    else if (test && cc == "ns") taken = lhs >= 0;
    else if (!test && cc == "b") taken = ul < ur;
    else if (!test && cc == "be") taken = ul <= ur;
    else if (!test && cc == "a") taken = ul > ur;
    else if (!test && cc == "ae") taken = ul >= ur;
    else return false;
    return true;
}

}

bool threadJumps(Function &fn) {
    Stream &s = fn.stream;
    Flow &f = fn.flow;
    f.live(s);
    const std::map<std::string, int> heads = labelBlocks(s, f);
    bool changed = false;
    // A label this pass makes is named past every one the function has, dead ones included, as rounds run it again.
    std::set<std::string> names;
    for (const Entry &e : s) if (e.kind == Entry::Label) names.insert(e.label);
    int made = 0;
    auto fresh = [&](const char *kind) {
        std::string name;
        do name = ".L." + fn.name + kind + std::to_string(made++); while (names.count(name));
        names.insert(name);
        return name;
    };
    // **A jump to a block that only jumps on** goes where that one goes.
    for (Entry &e : s) {
        if (e.kind != Entry::Ins || e.dead || !isJump(e.ins)) continue;
        const auto it = heads.find(e.ins.a.text);
        if (it == heads.end()) continue;
        const std::vector<int> only = liveInstrs(s, f.blocks[it->second]);
        if (only.size() != 1 || s[only[0]].ins.m != "jmp" || s[only[0]].ins.a.kind != Operand::Label) continue;
        if (s[only[0]].ins.a.text == e.ins.a.text) continue;
        e.ins.a.text = s[only[0]].ins.a.text;
        changed = true;
    }
    // **A constant carried into a compare-and-branch decides it here**: the
    // jump into it goes straight to the side taken, the flags dead there.
    struct Insert { int at; Entry e; };
    std::vector<Insert> inserts;
    for (int b = 0; b < static_cast<int>(f.blocks.size()); ++b) {
        const Block &blk = f.blocks[b];
        const std::vector<int> own = liveInstrs(s, blk);
        int last = own.empty() ? -1 : own.back();
        const bool jumps = last >= 0 && s[last].ins.m == "jmp" && s[last].ins.a.kind == Operand::Label;
        int t;
        if (jumps) { const auto it = heads.find(s[last].ins.a.text); if (it == heads.end()) continue; t = it->second; }
        else { if (last >= 0 && !controlOf(s[last].ins).falls) continue; t = b + 1; }
        if (t < 0 || t >= static_cast<int>(f.blocks.size()) || t + 1 >= static_cast<int>(f.blocks.size())) continue;
        const std::vector<int> pair = liveInstrs(s, f.blocks[t]);
        if (pair.size() != 2) continue;
        const Instr &cmp = s[pair[0]].ins, &jcc = s[pair[1]].ins;
        const std::string cc = conditionOf(jcc.m);
        if (cc.empty() || jcc.a.kind != Operand::Label || !gpr(cmp.b)) continue;
        long long value;
        bool taken;
        if (!constantAtEnd(s, f, blk, cmp.b.reg.id, value) || !decides(cmp, cc, value, taken)) continue;
        const auto target = heads.find(jcc.a.text);
        if (target == heads.end() || f.blocks[target->second].in.flags || f.blocks[t + 1].in.flags) continue;
        if (jumps) {
            if (taken) { s[last].ins.a.text = jcc.a.text; changed = true; continue; }
            const Block &fall = f.blocks[t + 1];
            std::string name;
            for (int k = fall.begin; k < fall.end && s[k].kind != Entry::Ins; ++k)
                if (s[k].kind == Entry::Label && !s[k].dead) { name = s[k].label; break; }
            if (name.empty()) {
                name = fresh(".thr.");
                Entry l;
                l.kind = Entry::Label;
                l.label = name;
                inserts.push_back(Insert{fall.begin, l});
                fn.jumpOnly.insert(name);
            }
            s[last].ins.a.text = name;
            changed = true;
        } else if (taken) {
            Entry j;
            j.ins = Instr{"jmp", jcc.a, Operand(), 1};
            inserts.push_back(Insert{last >= 0 ? last + 1 : blk.end, j});
            changed = true;
        }
    }
    // **A loop's back edge takes its test with it**: `jmp head` where the head is only `cmp; jcc exit` and the exit
    // is what follows the jump becomes `cmp; j!cc body` - one branch a turn instead of two, the loop rotated.
    if (!changed && inserts.empty())
        for (int b = 0; b < static_cast<int>(f.blocks.size()) - 1; ++b) {
            const std::vector<int> own = liveInstrs(s, f.blocks[b]);
            if (own.empty()) continue;
            const int last = own.back();
            if (s[last].ins.m != "jmp" || s[last].ins.a.kind != Operand::Label) continue;
            const auto it = heads.find(s[last].ins.a.text);
            if (it == heads.end() || it->second >= b || it->second + 1 >= static_cast<int>(f.blocks.size())) continue;
            const int t = it->second;
            const std::vector<int> pair = liveInstrs(s, f.blocks[t]);
            if (pair.size() != 2) continue;
            const Instr cmp = s[pair[0]].ins, jcc = s[pair[1]].ins;
            const std::string cc = conditionOf(jcc.m);
            static const std::set<std::string> tests = {"cmp", "cmpb", "cmpw", "cmpl", "cmpq", "test", "testb", "testw", "testl", "testq"};
            if (cc.empty() || jcc.a.kind != Operand::Label || !tests.count(cmp.m)) continue;
            const auto exit = heads.find(jcc.a.text);
            if (exit == heads.end() || exit->second != b + 1) continue;
            const Block &body = f.blocks[t + 1];
            if (body.in.flags || f.blocks[b + 1].in.flags) continue;
            std::string name;
            for (int k = body.begin; k < body.end && s[k].kind != Entry::Ins; ++k)
                if (s[k].kind == Entry::Label && !s[k].dead) { name = s[k].label; break; }
            if (name.empty()) {
                name = fresh(".rot.");
                Entry l;
                l.kind = Entry::Label;
                l.label = name;
                inserts.push_back(Insert{body.begin, l});
                fn.jumpOnly.insert(name);
            }
            s[last].ins = cmp;
            Entry back;
            Operand to = jcc.a;
            to.text = name;
            back.ins = Instr{"j" + inverse(cc), to, Operand(), 1};
            inserts.push_back(Insert{last + 1, back});
            changed = true;
        }
    // Highest index first, and at one index a label goes in before the jump
    // that precedes it, which the reversed push order gives.
    std::stable_sort(inserts.begin(), inserts.end(), [](const Insert &x, const Insert &y) { return x.at > y.at; });
    for (const Insert &in : inserts) s.insert(s.begin() + in.at, in.e);
    return changed;
}

}
