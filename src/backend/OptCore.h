#pragma once

// **What the optimizer of every instruction set shares.** An instruction set
// brings its instructions, its effects table and `controlOf`, found by
// argument lookup; the flow graph, liveness and dead code are the same for all.

#include <map>
#include <string>
#include <vector>

namespace opt {

// Up to 64 registers: x86's 16 general and 16 xmm, or arm64's 32 and 32.
using RegSet = unsigned long long;
constexpr RegSet bit(int r) { return 1ull << static_cast<unsigned>(r); }
constexpr RegSet kAllRegs = ~0ull;
// Register numbers from here up are pseudos, which a RegSet does not hold.
constexpr int kPhysical = 64;

struct Effects {
    RegSet reads = 0;        // every register whose value is used
    RegSet writes = 0;       // registers given a wholly new value
    RegSet partial = 0;      // written in part, so also read
    RegSet wide = 0;         // read above their low four bytes
    bool flagsRead = false;
    bool flagsWritten = false;
    bool memoryRead = false;
    bool memoryWritten = false;
    bool control = false;    // a jump, a call or a return
    bool stack = false;      // moves the stack pointer
    bool opaque = false;
};

// How an instruction moves control: whether its block ends after it, whether
// it may fall through, the label it jumps to if it names one, and whether it
// returns - a jump naming no label goes somewhere this function cannot see.
struct Control {
    bool ends = false;
    bool falls = true;
    bool returns = false;
    std::string target;
};

// **One function, in the order it came**: instructions, labels, and events -
// whatever else was written there, replayed where it stood.
template <class I, class Payload>
struct EntryOf {
    enum Kind { Ins, Label, Event };
    Kind kind = Ins;
    I ins;
    std::string label;
    Payload event;
    bool dead = false;
    // A label where the exception state changes: see Spelling::stateLabel.
    bool state = false;
};

// **An edge of the flow graph**, as GCC has them: from one block to another,
// or out of the function, with the kind that says what crosses it.
//
// Return carries nothing but what the return instruction reads; Leave is a
// jump this function cannot see the end of (through a register, or to a
// label it does not hold), so everything is live across it. Eh is an
// exception edge from a call to its landing pad: none is made yet - a pad is
// still a block with no predecessors, and no pass runs in a function that
// has one - but every pass that walks edges already sees the kind.
struct Edge {
    enum Kind { Fallthrough, Jump, Return, Leave, Eh };
    int from = 0;
    int to = kExit;                      // a block index, or kExit
    Kind kind = Fallthrough;
    static constexpr int kExit = -1;
};

struct Block {
    int begin = 0, end = 0;              // entries [begin, end)
    std::vector<int> succs, preds;       // edge indices, in the order made
    bool leaves = false;                 // has a Leave edge: all live at its end
    RegSet liveIn = 0, liveOut = 0;
    RegSet wideIn = 0, wideOut = 0;      // live, and read above the low four bytes
    bool flagsIn = false, flagsOut = false;
    int idom = -1;                       // immediate dominator; -1 for the entry, or not computed
};

// **The function as blocks and edges, and what is live across each.** A
// block starts at a label and ends after a jump or a return; liveness is
// solved over the edges to a fixpoint, so a value is dead only if no path
// reads it. Dominators are computed on request and kept until the next build.
template <class Entry>
struct FlowOf {
    std::vector<Block> blocks;
    std::vector<Edge> edges;
    std::vector<Effects> effects;        // one per entry; empty for labels and events
    bool dominated = false;              // whether each block's idom is current

    // The successor blocks of b, each once, in edge order; the exit left out.
    std::vector<int> succBlocks(int b) const {
        std::vector<int> out;
        for (int e : blocks[b].succs)
            if (edges[e].to != Edge::kExit) out.push_back(edges[e].to);
        return out;
    }
    std::vector<int> predBlocks(int b) const {
        std::vector<int> out;
        for (int e : blocks[b].preds) out.push_back(edges[e].from);
        return out;
    }

    template <class EffectsFn>
    void build(const std::vector<Entry> &s, EffectsFn effectsOf) {
        blocks.clear();
        edges.clear();
        dominated = false;
        effects.assign(s.size(), Effects());
        std::map<std::string, int> at;
        Block cur;
        for (int k = 0; k < static_cast<int>(s.size()); ++k) {
            const Entry &e = s[k];
            if (e.kind == Entry::Label && e.dead) continue;       // dropped: it joins
            if (e.kind == Entry::Label && k > cur.begin) {
                cur.end = k;
                blocks.push_back(cur);
                cur = Block();
                cur.begin = k;
            }
            if (e.kind == Entry::Label) at[e.label] = static_cast<int>(blocks.size());
            if (e.kind != Entry::Ins) continue;
            effects[k] = effectsOf(e.ins);
            if (!e.dead && controlOf(e.ins).ends) {
                cur.end = k + 1;
                blocks.push_back(cur);
                cur = Block();
                cur.begin = k + 1;
            }
        }
        cur.end = static_cast<int>(s.size());
        if (cur.end > cur.begin || blocks.empty()) blocks.push_back(cur);

        // Edges: the jump's target, and the next block if the last live
        // instruction can fall through.
        for (std::size_t b = 0; b < blocks.size(); ++b) {
            Control c;
            for (int k = blocks[b].end - 1; k >= blocks[b].begin; --k)
                if (s[k].kind == Entry::Ins && !s[k].dead) { c = controlOf(s[k].ins); break; }
            if (c.ends && !c.target.empty()) {
                const auto t = at.find(c.target);
                if (t != at.end()) connect(static_cast<int>(b), t->second, Edge::Jump);
                else connect(static_cast<int>(b), Edge::kExit, Edge::Leave);
            } else if (c.ends && !c.returns) {
                connect(static_cast<int>(b), Edge::kExit, Edge::Leave);
            } else if (c.ends && c.returns) {
                connect(static_cast<int>(b), Edge::kExit, Edge::Return);
            }
            if (!c.ends || c.falls) {
                if (b + 1 < blocks.size()) connect(static_cast<int>(b), static_cast<int>(b + 1), Edge::Fallthrough);
                else connect(static_cast<int>(b), Edge::kExit, Edge::Leave);
            }
        }
    }

    void connect(int from, int to, Edge::Kind kind) {
        Edge e;
        e.from = from;
        e.to = to;
        e.kind = kind;
        edges.push_back(e);
        const int id = static_cast<int>(edges.size()) - 1;
        blocks[from].succs.push_back(id);
        if (to != Edge::kExit) blocks[to].preds.push_back(id);
        if (kind == Edge::Leave) blocks[from].leaves = true;
    }

    void solve(const std::vector<Entry> &s) {
        for (Block &b : blocks) { b.liveIn = b.wideIn = 0; b.flagsIn = false; }
        for (bool changed = true; changed;) {
            changed = false;
            for (int b = static_cast<int>(blocks.size()) - 1; b >= 0; --b) {
                Block &blk = blocks[b];
                RegSet live = blk.leaves ? kAllRegs : 0, wide = live;
                bool flags = blk.leaves;
                for (int e : blk.succs) {
                    if (edges[e].to == Edge::kExit) continue;
                    const Block &n = blocks[edges[e].to];
                    live |= n.liveIn;
                    wide |= n.wideIn;
                    flags = flags || n.flagsIn;
                }
                blk.liveOut = live;
                blk.wideOut = wide;
                blk.flagsOut = flags;
                for (int k = blk.end - 1; k >= blk.begin; --k) {
                    if (s[k].kind != Entry::Ins || s[k].dead) continue;
                    const Effects &e = effects[k];
                    live = (live & ~e.writes) | e.reads;
                    wide = (wide & ~e.writes) | e.wide;
                    flags = e.flagsRead || (flags && !e.flagsWritten);
                }
                if (live != blk.liveIn || wide != blk.wideIn || flags != blk.flagsIn) {
                    blk.liveIn = live;
                    blk.wideIn = wide;
                    blk.flagsIn = flags;
                    changed = true;
                }
            }
        }
    }

    // **Immediate dominators**, by the iterative algorithm over a reverse
    // postorder (Cooper, Harvey and Kennedy): block 0 is the entry; a block
    // no path from it reaches, a landing pad today, has no dominator.
    void dominators() {
        if (dominated) return;
        const int n = static_cast<int>(blocks.size());
        std::vector<int> order, postIndex(n, -1);
        std::vector<int> stack, next(n, 0);
        std::vector<bool> seen(n, false);
        if (n > 0) { stack.push_back(0); seen[0] = true; }
        while (!stack.empty()) {
            const int b = stack.back();
            const std::vector<int> s = succBlocks(b);
            if (next[b] < static_cast<int>(s.size())) {
                const int t = s[next[b]++];
                if (!seen[t]) { seen[t] = true; stack.push_back(t); }
            } else {
                postIndex[b] = static_cast<int>(order.size());
                order.push_back(b);
                stack.pop_back();
            }
        }
        for (Block &b : blocks) b.idom = -1;
        auto intersect = [&](int x, int y) {
            while (x != y) {
                while (postIndex[x] < postIndex[y]) x = blocks[x].idom;
                while (postIndex[y] < postIndex[x]) y = blocks[y].idom;
            }
            return x;
        };
        if (n > 0) blocks[0].idom = 0;
        for (bool changed = true; changed;) {
            changed = false;
            for (int i = static_cast<int>(order.size()) - 1; i >= 0; --i) {
                const int b = order[i];
                if (b == 0) continue;
                int idom = -1;
                for (int p : predBlocks(b)) {
                    if (postIndex[p] < 0 || blocks[p].idom < 0) continue;
                    idom = idom < 0 ? p : intersect(p, idom);
                }
                if (idom != blocks[b].idom) { blocks[b].idom = idom; changed = true; }
            }
        }
        if (n > 0) blocks[0].idom = -1;
        dominated = true;
    }

    // Whether block a dominates block b: b, or a proper ancestor in the tree.
    bool dominates(int a, int b) const {
        for (int x = b; x >= 0; x = blocks[x].idom)
            if (x == a) return true;
        return false;
    }
};

// **An instruction whose results nobody reads, and that touches nothing
// else.** `frame` names the registers that are never taken for dead; `idle`
// says of an instruction that only the upper halves it writes go unread.
template <class Entry, class IdleFn>
bool removeDeadIn(std::vector<Entry> &s, FlowOf<Entry> &f, RegSet frame, IdleFn idle) {
    f.solve(s);
    bool changed = false;
    for (const Block &blk : f.blocks) {
        RegSet live = blk.liveOut, wide = blk.wideOut;
        bool flags = blk.flagsOut;
        for (int k = blk.end - 1; k >= blk.begin; --k) {
            Entry &en = s[k];
            if (en.kind != Entry::Ins || en.dead) continue;
            const Effects &e = f.effects[k];
            const bool removable = !e.control && !e.memoryWritten && !e.stack && !e.opaque &&
                                   ((e.writes | e.partial) & frame) == 0;
            const bool used = ((e.writes | e.partial) & live) != 0 || (e.flagsWritten && flags);
            if ((removable && !used) || idle(en.ins, wide)) {
                en.dead = true;
                changed = true;
                continue;
            }
            live = (live & ~e.writes) | e.reads;
            wide = (wide & ~e.writes) | e.wide;
            flags = e.flagsRead || (flags && !e.flagsWritten);
        }
    }
    return changed;
}

// Code no label leads to after a jump or a return, and a jump to the label
// that follows it anyway.
template <class Entry>
bool removeUnreachableIn(std::vector<Entry> &s) {
    bool changed = false, gone = false;
    for (std::size_t k = 0; k < s.size(); ++k) {
        Entry &en = s[k];
        if (en.kind == Entry::Label && !en.dead) gone = false;
        if (en.kind != Entry::Ins || en.dead) continue;
        if (gone) { en.dead = changed = true; continue; }
        const Control c = controlOf(en.ins);
        if (!c.ends || c.falls) continue;
        gone = true;
        if (c.target.empty()) continue;
        for (std::size_t j = k + 1; j < s.size(); ++j) {
            if (s[j].kind == Entry::Label && !s[j].dead && s[j].label == c.target) { en.dead = changed = true; break; }
            if (s[j].kind == Entry::Ins && !s[j].dead) break;
        }
    }
    return changed;
}

}
