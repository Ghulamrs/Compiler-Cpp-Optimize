#include "Mir.h"

#include "OptDataflow.h"

#include <algorithm>

namespace mir {

using namespace opt;

namespace {

// The registers webs are made of: the general ones but the frame's.
bool candidate(int r) { return r >= 0 && r < kGprs && !frameReg(r); }

bool isShift(const std::string &m) { return opcodeOf(m).has(Opcode::kShift); }

// One operand naming a candidate register, and what the instruction does there.
struct Occurrence {
    int entry = 0;
    int operand = 0;            // 0 for a, 1 for b
    int reg = 0;
    bool read = false;
    bool write = false;         // a new value, whole or in part
    bool keeps = false;         // written in part: the old value is read too
    bool pinned = false;        // the instruction wants this very register
    bool tied = false;          // no role of its own: named as the other's write
};

// **What each operand of an instruction does with a candidate register.** An
// operand with no role (xor's first, zeroing) is tied to the other's write.
std::vector<Occurrence> occurrencesOf(const Instr &i, int entry) {
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

// Union-find over definitions; a pin on any member pins the web.
struct Sets {
    std::vector<int> parent;
    std::vector<bool> pinned;
    int make(bool pin) {
        parent.push_back(static_cast<int>(parent.size()));
        pinned.push_back(pin);
        return parent.back();
    }
    int find(int x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    }
    void unite(int x, int y) {
        x = find(x);
        y = find(y);
        if (x == y) return;
        parent[y] = x;
        pinned[x] = pinned[x] || pinned[y];
    }
    void pin(int x) { pinned[find(x)] = true; }
};

}

// The flow must describe the stream: the pass that calls this has it built.
Webs buildWebs(Stream &s, Flow &f, const Convention &c) {
    (void)c;
    const int nb = static_cast<int>(f.blocks.size());

    Sets sets;
    // **A block entered from nowhere this function shows** - the entry, or a
    // label only a table names - starts from a value the ABI placed: pinned.
    std::vector<std::vector<int>> unknownIn(nb, std::vector<int>(kGprs, -1));
    for (int b = 0; b < nb; ++b)
        if (b == 0 || f.blocks[b].preds.empty())
            for (int r = 0; r < kGprs; ++r)
                if (candidate(r)) unknownIn[b][r] = sets.make(true);

    std::vector<std::vector<Occurrence>> occ(s.size());
    for (int k = 0; k < static_cast<int>(s.size()); ++k)
        if (s[k].kind == Entry::Ins && !s[k].dead) occ[k] = occurrencesOf(s[k].ins, k);

    typedef ReachingDefs::DefList DefList;
    std::vector<std::vector<int>> defAt(s.size(), std::vector<int>(2, -1));
    std::vector<std::vector<int>> implicitDef(s.size());

    // Every definition is made once, before the fixpoint, so its identity is stable.
    for (int k = 0; k < static_cast<int>(s.size()); ++k) {
        if (s[k].kind != Entry::Ins || s[k].dead) continue;
        RegSet named = 0;
        for (const Occurrence &o : occ[k])
            if (o.write) { defAt[k][o.operand] = sets.make(false); named |= bit(o.reg); }
        const Effects &e = f.effects[k];
        implicitDef[k].assign(kGprs, -1);
        for (int r = 0; r < kGprs; ++r)
            if (candidate(r) && ((e.writes | e.partial) & bit(r)) && !(named & bit(r)))
                implicitDef[k][r] = sets.make(true);
    }

    // The definition of r each instruction leaves, if it makes one; then the
    // reaching definitions solved over the flow from that.
    std::vector<std::vector<int>> lastDef(s.size(), std::vector<int>(kGprs, -1));
    for (int k = 0; k < static_cast<int>(s.size()); ++k) {
        if (implicitDef[k].empty()) continue;
        for (int r = 0; r < kGprs; ++r) {
            int d = implicitDef[k][r];
            for (const Occurrence &o : occ[k]) if (o.write && o.reg == r) { d = defAt[k][o.operand]; break; }
            lastDef[k][r] = d;
        }
    }
    ReachingDefs rd;
    rd.solve(f, s, kGprs, lastDef, unknownIn);
    const std::vector<std::vector<DefList>> &in = rd.in;

    // **Walk each block with what reaches it, joining each use to its
    // definitions.** A read the instruction makes implicitly, or at a place
    // that must be this register, pins what it reads.
    std::vector<std::vector<int>> useWeb(s.size(), std::vector<int>(2, -1));
    for (int b = 0; b < nb; ++b) {
        std::vector<DefList> reach = in[b];
        for (int k = f.blocks[b].begin; k < f.blocks[b].end; ++k) {
            if (s[k].kind != Entry::Ins || s[k].dead) continue;
            const Effects &e = f.effects[k];
            RegSet namedRead = 0;
            for (const Occurrence &o : occ[k]) {
                if (!o.read && !o.keeps) continue;
                namedRead |= bit(o.reg);
                const DefList &from = reach[o.reg];
                if (from.empty()) { useWeb[k][o.operand] = sets.make(true); continue; }
                for (int d : from) sets.unite(from[0], d);
                if (o.pinned) sets.pin(from[0]);
                useWeb[k][o.operand] = from[0];
            }
            for (int r = 0; r < kGprs; ++r)
                if (candidate(r) && (e.reads & bit(r)) && !(namedRead & bit(r)))
                    for (int d : reach[r]) sets.pin(d);
            // A write joins the use in the same operand: one name for both.
            for (const Occurrence &o : occ[k]) {
                if (!o.write) continue;
                const int d = defAt[k][o.operand];
                if (useWeb[k][o.operand] >= 0) sets.unite(useWeb[k][o.operand], d);
                reach[o.reg].assign(1, d);
            }
            for (int r = 0; r < kGprs; ++r)
                if (implicitDef[k][r] >= 0) reach[r].assign(1, implicitDef[k][r]);
            // An operand with no role takes the name of this instruction's write.
            for (const Occurrence &o : occ[k]) {
                if (!o.tied) continue;
                const int other = defAt[k][1 - o.operand];
                useWeb[k][o.operand] = other >= 0 ? other : sets.make(true);
            }
        }
        // What leaves for somewhere unseen may be read there.
        if (f.blocks[b].leaves)
            for (int r = 0; r < kGprs; ++r)
                for (int d : reach[r]) sets.pin(d);
    }

    // **Every unpinned web a pseudo**, and each operand renamed to its web's.
    Webs webs;
    std::vector<int> pseudoOf(sets.parent.size(), -1);
    for (int k = 0; k < static_cast<int>(s.size()); ++k) {
        for (const Occurrence &o : occ[k]) {
            const int web = defAt[k][o.operand] >= 0 ? defAt[k][o.operand] : useWeb[k][o.operand];
            if (web < 0) continue;
            const int root = sets.find(web);
            if (sets.pinned[root]) continue;
            if (pseudoOf[root] < 0) {
                pseudoOf[root] = kFirstPseudo + webs.count();
                webs.home.push_back(o.reg);
            }
            Operand &op = o.operand == 0 ? s[k].ins.a : s[k].ins.b;
            op.reg.id = pseudoOf[root];
        }
    }
    return webs;
}

void assign(Stream &s, const std::vector<int> &colour) {
    for (Entry &e : s) {
        if (e.kind != Entry::Ins) continue;
        for (Operand *o : {&e.ins.a, &e.ins.b})
            if (isPseudo(o->reg.id)) o->reg.id = colour[o->reg.id - kFirstPseudo];
    }
}

}
