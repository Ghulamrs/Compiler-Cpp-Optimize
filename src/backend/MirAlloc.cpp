#include "MirAlloc.h"

#include <algorithm>
#include <cassert>

namespace mir {

using namespace opt;

namespace {

// The registers a pseudo may be given: the general ones but the frame's.
bool candidate(int r) { return r >= 0 && r < kGprs && !frameReg(r); }

bool wholeReg(const Operand &o) { return o.kind == Operand::Register && o.reg.width == 8; }

// A whole copy between two registers, at least one of them a pseudo; the
// operands else -1. Assigned alike, such a copy is of a register to itself.
bool copyOf(const Instr &i, int &src, int &dst) {
    if (!isMovQ(i.m) || i.operands != 2 || !wholeReg(i.a) || !wholeReg(i.b)) return false;
    src = i.a.reg.id;
    dst = i.b.reg.id;
    if (!isPseudo(src) && !isPseudo(dst)) return false;
    return (isPseudo(src) || candidate(src)) && (isPseudo(dst) || candidate(dst));
}

// Pseudos are numbered from kFirstPseudo; the graph numbers them from 0.
int indexOf(int reg) { return reg - kFirstPseudo; }

}

Allocator::Allocator(Function &fn, const std::vector<int> &homes) : fn_(fn), homes_(homes) {
    n_ = static_cast<int>(homes.size());
    words_ = (n_ + 63) / 64;
}

// **What each instruction does with each pseudo it names**: a register
// operand by its role, a memory or indirect operand's base read; a write
// narrower than four bytes keeps the rest, so reads too.
void Allocator::findUses() {
    const Stream &s = fn_.stream;
    uses_.assign(s.size(), std::vector<Use>());
    copies_.clear();
    for (int k = 0; k < static_cast<int>(s.size()); ++k) {
        if (s[k].kind != Entry::Ins || s[k].dead) continue;
        const Instr &i = s[k].ins;
        const Roles roles = rolesOf(i);
        const Operand *ops[2] = {&i.a, &i.b};
        const unsigned role[2] = {roles.a, roles.b};
        for (int n = 0; n < 2; ++n) {
            const Operand &o = *ops[n];
            if (!isPseudo(o.reg.id)) continue;
            Use u{indexOf(o.reg.id), false, false};
            if (o.kind == Operand::Register) {
                if (role[n] == 0) continue;
                u.read = (role[n] & kRead) != 0;
                u.write = (role[n] & kWrite) != 0;
                if (u.write && ((role[n] & kKeep) || o.reg.width < 4)) u.read = true;
            } else if (o.kind == Operand::Memory || o.kind == Operand::Indirect) {
                u.read = true;
            } else {
                continue;
            }
            uses_[k].push_back(u);
        }
        int src, dst;
        if (copyOf(i, src, dst)) copies_.push_back(Copy{k, src, dst, 0});
    }
}

// **Liveness over the pseudos**, the same problem the flow solves over the
// physical registers, one bit per pseudo: to a fixpoint backward over the
// blocks, and what a pad reads joined at the call that may leave for it.
void Allocator::solveLiveness() {
    const Stream &s = fn_.stream;
    const Flow &f = fn_.flow;
    const int nb = static_cast<int>(f.blocks.size());
    liveIn_.assign(nb, Bits(words_, 0));
    liveOut_.assign(nb, Bits(words_, 0));
    for (bool changed = true; changed;) {
        changed = false;
        for (int b = nb - 1; b >= 0; --b) {
            const Block &blk = f.blocks[b];
            Bits live(words_, 0);
            for (int e : blk.succs)
                if (f.edges[e].kind != Edge::Eh && f.edges[e].to != Edge::kExit)
                    for (int w = 0; w < words_; ++w) live[w] |= liveIn_[f.edges[e].to][w];
            liveOut_[b] = live;
            for (int k = blk.end - 1; k >= blk.begin; --k) {
                if (s[k].kind != Entry::Ins || s[k].dead) continue;
                for (int e : blk.succs)
                    if (f.edges[e].kind == Edge::Eh && f.edges[e].at == k && f.edges[e].to != Edge::kExit)
                        for (int w = 0; w < words_; ++w) live[w] |= liveIn_[f.edges[e].to][w];
                for (const Use &u : uses_[k]) if (u.write) clear(live, u.pseudo);
                for (const Use &u : uses_[k]) if (u.read) set(live, u.pseudo);
            }
            if (live != liveIn_[b]) { liveIn_[b] = live; changed = true; }
        }
    }
}

void Allocator::addEdge(int p, int q) {
    if (p == q || interferes(p, q)) return;
    matrix_[static_cast<std::size_t>(p) * words_ + q / 64] |= 1ull << (q % 64);
    matrix_[static_cast<std::size_t>(q) * words_ + p / 64] |= 1ull << (p % 64);
    nodes_[p].adj.push_back(q);
    nodes_[q].adj.push_back(p);
}

// **A pseudo defined here interferes with every pseudo live after, but for
// the source of a copy** (Chaitin), and may not take a physical register
// live after, nor one written here; one live across may not be in one written here.
void Allocator::buildInterference() {
    const Stream &s = fn_.stream;
    Flow &f = fn_.flow;
    // The physical liveness is the flow's: a pseudo's write kills no
    // physical register, which one it kills being what is decided here.
    f.live(s);
    nodes_.assign(n_, Node());
    matrix_.assign(static_cast<std::size_t>(n_) * words_, 0);
    for (int p = 0; p < n_; ++p) nodes_[p].home = homes_[p];
    std::vector<int> live;
    for (int b = 0; b < static_cast<int>(f.blocks.size()); ++b) {
        const Block &blk = f.blocks[b];
        Live phys = blk.out;
        Bits bits = liveOut_[b];
        for (int k = blk.end - 1; k >= blk.begin; --k) {
            if (s[k].kind != Entry::Ins || s[k].dead) continue;
            f.joinPads(b, k, phys);
            for (int e : blk.succs)
                if (f.edges[e].kind == Edge::Eh && f.edges[e].at == k && f.edges[e].to != Edge::kExit)
                    for (int w = 0; w < words_; ++w) bits[w] |= liveIn_[f.edges[e].to][w];
            live.clear();
            for (int p = 0; p < n_; ++p) if (has(bits, p)) live.push_back(p);
            const Effects &e = f.effects[k];
            const RegSet written = e.writes | e.partial;
            int src = -1, dst = -1;
            copyOf(s[k].ins, src, dst);
            for (int p : live) nodes_[p].forbid |= written;
            for (const Use &u : uses_[k]) {
                if (!u.write) continue;
                for (int q : live)
                    if (!(isPseudo(src) && q == indexOf(src) && u.pseudo == indexOf(dst))) addEdge(u.pseudo, q);
                RegSet physLive = phys.regs;
                if (!isPseudo(src) && src >= 0 && u.pseudo == indexOf(dst)) physLive &= ~bit(src);
                nodes_[u.pseudo].forbid |= physLive | written;
            }
            phys.step(e);
            for (const Use &u : uses_[k]) if (u.write) clear(bits, u.pseudo);
            for (const Use &u : uses_[k]) if (u.read) set(bits, u.pseudo);
        }
        // What is live into a block entered from nowhere this function shows
        // was placed by nobody here: those pseudos interfere with each other
        // and with every physical register live there.
        if (b != 0 && !blk.preds.empty()) continue;
        live.clear();
        for (int p = 0; p < n_; ++p) if (has(liveIn_[b], p)) live.push_back(p);
        for (int p : live) {
            nodes_[p].forbid |= blk.in.regs;
            for (int q : live) addEdge(p, q);
        }
    }
    for (Node &nd : nodes_) nd.forbid &= ~(bit(RSP) | bit(RBP));
}

// **A reference counts by where it stands**: the level says what one is
// worth at each loop depth. A copy's weight is what coalescing it saves.
void Allocator::collectCosts() {
    const Stream &s = fn_.stream;
    const Flow &f = fn_.flow;
    const Loops &loops = fn_.loops();
    const Costs &costs = fn_.costs();
    std::size_t c = 0;
    for (int b = 0; b < static_cast<int>(f.blocks.size()); ++b) {
        const long w = costs.referenceWeight(loops.depthOf(b));
        for (int k = f.blocks[b].begin; k < f.blocks[b].end; ++k) {
            if (s[k].kind != Entry::Ins || s[k].dead) continue;
            for (const Use &u : uses_[k]) nodes_[u.pseudo].weight += w;
            while (c < copies_.size() && copies_[c].entry < k) ++c;
            if (c < copies_.size() && copies_[c].entry == k) copies_[c++].weight = w;
        }
    }
}

// Whether a colouring honours the graph: no interfering pair shares a
// register, and no pseudo has one it may not take.
bool Allocator::valid(const std::vector<int> &colour) const {
    for (int p = 0; p < n_; ++p) {
        if (!candidate(colour[p]) || (nodes_[p].forbid & bit(colour[p]))) return false;
        for (int q : nodes_[p].adj) if (colour[q] == colour[p]) return false;
    }
    return true;
}

bool Allocator::run() {
    coalesced_ = 0;
    fellBack_ = false;
    if (n_ == 0) return Webs::dropSelfCopies(fn_);
    findUses();
    solveLiveness();
    buildInterference();
    collectCosts();
    colour_ = homes_;
    assert(valid(colour_) && "the homes must colour the graph: they are how the stream ran");
    Webs::assign(fn_, colour_);
    return Webs::dropSelfCopies(fn_);
}

}
