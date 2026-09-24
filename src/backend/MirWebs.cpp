#include "Mir.h"

#include "OptDataflow.h"


namespace mir {

using namespace opt;

namespace {

// The registers webs are made of: the general ones but the frame's.
bool candidate(int r) { return r >= 0 && r < kGprs && !frameReg(r); }

bool isShift(const std::string &m) { return opcodeOf(m).has(Opcode::kShift); }

// A copy of a register to itself, whole: the one move that is truly
// nothing, where a four-byte one zero-extends.
Entry copy(int reg) {
    Entry e;
    e.ins = Instr{"mov", Operand::ofReg(reg, 8), Operand::ofReg(reg, 8), 2};
    return e;
}

}

int Webs::Sets::make(bool pin) {
    parent_.push_back(static_cast<int>(parent_.size()));
    pinned_.push_back(pin);
    return parent_.back();
}

int Webs::Sets::find(int x) {
    while (parent_[x] != x) x = parent_[x] = parent_[parent_[x]];
    return x;
}

void Webs::Sets::unite(int x, int y) {
    x = find(x);
    y = find(y);
    if (x == y) return;
    parent_[y] = x;
    pinned_[x] = pinned_[x] || pinned_[y];
}

Webs::Webs(Function &fn) : fn_(fn) {}

// **What each operand of an instruction does with a candidate register.** An
// operand with no role (xor's first, zeroing) is tied to the other's write.
std::vector<Webs::Occurrence> Webs::occurrencesOf(const Instr &i, int entry) {
    std::vector<Occurrence> out;
    const Roles roles = rolesOf(i);
    const Operand *ops[2] = {&i.a, &i.b};
    const unsigned role[2] = {roles.a, roles.b};
    for (int n = 0; n < 2; ++n) {
        const Operand &o = *ops[n];
        if (!candidate(o.reg.id)) continue;
        Occurrence oc;
        oc.entry = entry;
        oc.operand = n;
        oc.reg = o.reg.id;
        if (o.kind == Operand::Register) {
            oc.read = (role[n] & kRead) != 0;
            oc.write = (role[n] & kWrite) != 0;
            oc.keeps = oc.write && ((role[n] & kKeep) || o.reg.width < 4);
            oc.pinned = n == 0 && isShift(i.m);
            oc.tied = role[n] == 0;
        } else if (o.kind == Operand::Memory || o.kind == Operand::Indirect) {
            oc.read = true;
        } else {
            continue;
        }
        out.push_back(oc);
    }
    return out;
}

RegSet Webs::implicitReads(int k) const {
    RegSet named = 0;
    for (const Occurrence &o : occ_[k]) if (o.read || o.keeps) named |= bit(o.reg);
    return fn_.flow.effects[k].reads & ~named;
}

RegSet Webs::implicitWrites(int k) const {
    RegSet named = 0;
    for (const Occurrence &o : occ_[k]) if (o.write) named |= bit(o.reg);
    const Effects &e = fn_.flow.effects[k];
    return (e.writes | e.partial) & ~named;
}

// **Where an instruction reads a register it does not name, the value is
// copied there just before; where it writes one that is read after, the
// value is copied out just after** - a shift's count in %cl among the
// former, since the operand names the register but no other would do. A
// return's implicit reads are the calling convention's own protocol - the
// result in rax, the callee-saved registers restored - and stay as they are.
bool Webs::splitPinned() {
    Stream &s = fn_.stream;
    Flow &f = fn_.flow;
    f.live(s);
    occ_.assign(s.size(), std::vector<Occurrence>());
    for (int k = 0; k < static_cast<int>(s.size()); ++k)
        if (s[k].kind == Entry::Ins && !s[k].dead) occ_[k] = occurrencesOf(s[k].ins, k);

    Stream out;
    out.reserve(s.size() + s.size() / 8);
    int prologueAt = fn_.prologueAt;
    bool changed = false;
    for (int b = 0; b < static_cast<int>(f.blocks.size()); ++b) {
        const Block &blk = f.blocks[b];
        // What is live after each entry, walked backward once.
        std::vector<RegSet> liveAfter(blk.end - blk.begin, 0);
        Live live = blk.out;
        for (int k = blk.end - 1; k >= blk.begin; --k) {
            liveAfter[k - blk.begin] = live.regs;
            if (s[k].kind != Entry::Ins || s[k].dead) continue;
            f.joinPads(b, k, live);
            live.step(f.effects[k]);
        }
        for (int k = blk.begin; k < blk.end; ++k) {
            const bool ins = s[k].kind == Entry::Ins && !s[k].dead;
            const bool ret = ins && opcodeOf(s[k].ins.m).kind == Opcode::Ret;
            if (ins && !ret) {
                const RegSet wanted = implicitReads(k);
                for (int r = 0; r < kGprs; ++r)
                    if (candidate(r) && (wanted & bit(r))) { out.push_back(copy(r)); changed = true; }
                for (const Occurrence &o : occ_[k])
                    if (o.pinned) { out.push_back(copy(o.reg)); changed = true; }
            }
            if (k == fn_.prologueAt) prologueAt = static_cast<int>(out.size());
            out.push_back(std::move(s[k]));
            if (ins && !ret) {
                const RegSet leftLive = implicitWrites(k) & liveAfter[k - blk.begin];
                for (int r = 0; r < kGprs; ++r)
                    if (candidate(r) && (leftLive & bit(r))) { out.push_back(copy(r)); changed = true; }
            }
        }
    }
    if (!changed) { s.swap(out); return false; }
    s.swap(out);
    fn_.prologueAt = prologueAt;
    fn_.buildFlow();
    return true;
}

// Every definition is made once, before the fixpoint, so its identity is
// stable: one per operand that writes, one per register an instruction
// writes without naming it - the latter pinned, the ABI having placed it.
void Webs::makeDefinitions() {
    const Stream &s = fn_.stream;
    occ_.assign(s.size(), std::vector<Occurrence>());
    defAt_.assign(s.size(), std::vector<int>(2, -1));
    implicitDef_.assign(s.size(), std::vector<int>());
    for (int k = 0; k < static_cast<int>(s.size()); ++k) {
        if (s[k].kind != Entry::Ins || s[k].dead) continue;
        occ_[k] = occurrencesOf(s[k].ins, k);
        for (const Occurrence &o : occ_[k])
            if (o.write) defAt_[k][o.operand] = sets_.make(false);
        implicitDef_[k].assign(kGprs, -1);
        const RegSet unnamed = implicitWrites(k);
        for (int r = 0; r < kGprs; ++r)
            if (candidate(r) && (unnamed & bit(r))) implicitDef_[k][r] = sets_.make(true);
    }
    // **A block entered from nowhere this function shows** - the entry, or a
    // label only a table names - starts from a value the ABI placed: pinned.
    const int nb = static_cast<int>(fn_.flow.blocks.size());
    unknownIn_.assign(nb, std::vector<int>(kGprs, -1));
    for (int b = 0; b < nb; ++b)
        if (b == 0 || fn_.flow.blocks[b].preds.empty())
            for (int r = 0; r < kGprs; ++r)
                if (candidate(r)) unknownIn_[b][r] = sets_.make(true);
}

// **Walk each block with what reaches it, joining each use to its
// definitions.** A read the instruction makes implicitly, or at a place
// that must be this register, pins what it reads.
void Webs::joinUsesToDefinitions() {
    const Stream &s = fn_.stream;
    const Flow &f = fn_.flow;
    typedef ReachingDefs::DefList DefList;

    // The definition of r each instruction leaves, if it makes one; then the
    // reaching definitions solved over the flow from that.
    std::vector<std::vector<int>> lastDef(s.size(), std::vector<int>(kGprs, -1));
    for (int k = 0; k < static_cast<int>(s.size()); ++k) {
        if (implicitDef_[k].empty()) continue;
        for (int r = 0; r < kGprs; ++r) {
            int d = implicitDef_[k][r];
            for (const Occurrence &o : occ_[k]) if (o.write && o.reg == r) { d = defAt_[k][o.operand]; break; }
            lastDef[k][r] = d;
        }
    }
    ReachingDefs rd;
    rd.solve(f, s, kGprs, lastDef, unknownIn_);

    useWeb_.assign(s.size(), std::vector<int>(2, -1));
    for (int b = 0; b < static_cast<int>(f.blocks.size()); ++b) {
        std::vector<DefList> reach = rd.in[b];
        for (int k = f.blocks[b].begin; k < f.blocks[b].end; ++k) {
            if (s[k].kind != Entry::Ins || s[k].dead) continue;
            for (const Occurrence &o : occ_[k]) {
                if (!o.read && !o.keeps) continue;
                const DefList &from = reach[o.reg];
                if (from.empty()) { useWeb_[k][o.operand] = sets_.make(true); continue; }
                for (int d : from) sets_.unite(from[0], d);
                if (o.pinned) sets_.pin(from[0]);
                useWeb_[k][o.operand] = from[0];
            }
            const RegSet unnamed = implicitReads(k);
            for (int r = 0; r < kGprs; ++r)
                if (candidate(r) && (unnamed & bit(r)))
                    for (int d : reach[r]) sets_.pin(d);
            // A write joins the use in the same operand: one name for both.
            for (const Occurrence &o : occ_[k]) {
                if (!o.write) continue;
                const int d = defAt_[k][o.operand];
                if (useWeb_[k][o.operand] >= 0) sets_.unite(useWeb_[k][o.operand], d);
                reach[o.reg].assign(1, d);
            }
            for (int r = 0; r < kGprs; ++r)
                if (implicitDef_[k][r] >= 0) reach[r].assign(1, implicitDef_[k][r]);
            // An operand with no role takes the name of this instruction's write.
            for (const Occurrence &o : occ_[k]) {
                if (!o.tied) continue;
                const int other = defAt_[k][1 - o.operand];
                useWeb_[k][o.operand] = other >= 0 ? other : sets_.make(true);
            }
        }
        // What leaves for somewhere unseen may be read there.
        if (f.blocks[b].leaves)
            for (int r = 0; r < kGprs; ++r)
                for (int d : reach[r]) sets_.pin(d);
    }
}

// **Every unpinned web a pseudo**, and each operand renamed to its web's.
void Webs::renameToPseudos() {
    Stream &s = fn_.stream;
    std::vector<int> pseudoOf(sets_.size(), -1);
    std::vector<bool> counted(sets_.size(), false);
    for (int k = 0; k < static_cast<int>(s.size()); ++k) {
        for (const Occurrence &o : occ_[k]) {
            const int web = defAt_[k][o.operand] >= 0 ? defAt_[k][o.operand] : useWeb_[k][o.operand];
            if (web < 0) continue;
            const int root = sets_.find(web);
            if (sets_.isPinned(root)) {
                if (!counted[root]) { counted[root] = true; ++pinned_; }
                continue;
            }
            if (pseudoOf[root] < 0) {
                pseudoOf[root] = kFirstPseudo + count();
                home_.push_back(o.reg);
            }
            Operand &op = o.operand == 0 ? s[k].ins.a : s[k].ins.b;
            op.reg.id = pseudoOf[root];
            fn_.flow.effects[k] = effectsOf(s[k].ins, fn_.convention);
        }
    }
    fn_.flow.touch();
}

void Webs::build() {
    sets_ = Sets();
    home_.clear();
    pinned_ = 0;
    makeDefinitions();
    joinUsesToDefinitions();
    renameToPseudos();
}

// The entry's effects follow the rename, so the flow describes the stream still.
void Webs::assign(Function &fn, const std::vector<int> &colour) {
    for (int k = 0; k < static_cast<int>(fn.stream.size()); ++k) {
        Entry &e = fn.stream[k];
        if (e.kind != Entry::Ins) continue;
        bool renamed = false;
        for (Operand *o : {&e.ins.a, &e.ins.b})
            if (isPseudo(o->reg.id)) { o->reg.id = colour[o->reg.id - kFirstPseudo]; renamed = true; }
        if (renamed) fn.flow.effects[k] = effectsOf(e.ins, fn.convention);
    }
    fn.flow.touch();
}

bool Webs::dropSelfCopies(Function &fn) {
    bool changed = false;
    for (Entry &e : fn.stream) {
        if (e.kind != Entry::Ins || e.dead) continue;
        const Instr &i = e.ins;
        if (i.m == "mov" && i.operands == 2 && i.a.kind == Operand::Register && i.b.kind == Operand::Register &&
            i.a.reg.id == i.b.reg.id && i.a.reg.width == 8 && i.b.reg.width == 8) {
            e.dead = true;
            changed = true;
        }
    }
    return changed;
}

}
