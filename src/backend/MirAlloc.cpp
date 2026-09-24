#include "MirAlloc.h"

#include "OptPasses.h"

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

int popcount(unsigned long long x) {
    int n = 0;
    for (; x; x &= x - 1) ++n;
    return n;
}

// The registers offered, cheapest encoding first; the frame's never, and
// the preserved ones only for a save charged.
const int kOffer[] = {RAX, RCX, RDX, RSI, RDI, R8, R9, R10, R11,
                      RBX, R12, R13, R14, R15};

}

Allocator::Allocator(Function &fn) : fn_(fn), homes_(fn.homes) {
    nHomes_ = static_cast<int>(fn.homes.size());
    n_ = nHomes_ + static_cast<int>(fn.slots.size());
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
            // A local may be read wherever a jump this function cannot see lands.
            if (blk.leaves)
                for (int p = nHomes_; p < n_; ++p) if (!demoted_[p]) set(live, p);
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
    // The physical liveness is the flow's, in which a pseudo's write kills
    // no physical register, which one it kills being what is decided here.
    f.live(s);
    nodes_.assign(n_, Node());
    matrix_.assign(static_cast<std::size_t>(n_) * words_, 0);
    for (int p = 0; p < n_; ++p) {
        nodes_[p].home = p < nHomes_ ? homes_[p] : -1;
        nodes_[p].slot = isSlot(p);
    }
    // A preserved register the stream never names is offered with a save
    // and a restore, so its life from the entry to the return does not count.
    RegSet mentioned = 0;
    for (const Entry &e : s) {
        if (e.kind != Entry::Ins || e.dead) continue;
        for (const Operand *o : {&e.ins.a, &e.ins.b})
            if ((o->kind == Operand::Register || o->kind == Operand::Memory || o->kind == Operand::Indirect) &&
                o->reg.id >= 0 && o->reg.id < kPhysical)
                mentioned |= bit(o->reg.id);
        for (const Operand *o : {&e.ins.a, &e.ins.b})
            if (o->indexed()) mentioned |= bit(o->index.id);
    }
    offered_ = 0;
    for (int r = 0; r < kGprs; ++r)
        if (candidate(r) && (fn_.convention.preserved & bit(r)) && !(mentioned & bit(r))) offered_ |= bit(r);
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
                RegSet physLive = phys.regs & ~offered_;
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
            nodes_[p].forbid |= blk.in.regs & ~offered_;
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
        const long lw = costs.loopWeight(loops.depthOf(b));
        for (int k = f.blocks[b].begin; k < f.blocks[b].end; ++k) {
            if (s[k].kind != Entry::Ins || s[k].dead) continue;
            for (const Use &u : uses_[k]) {
                nodes_[u.pseudo].weight += w;
                if (isSlot(u.pseudo)) nodes_[u.pseudo].slotWeight += lw;
            }
            while (c < copies_.size() && copies_[c].entry < k) ++c;
            if (c < copies_.size() && copies_[c].entry == k) copies_[c++].weight = w;
        }
    }
}

// Whether a colouring honours the graph: no interfering pair shares a
// register, and no pseudo has one it may not take; a slot is always right.
bool Allocator::valid(const std::vector<int> &colour) const {
    for (int p = 0; p < n_; ++p) {
        if (colour[p] < 0) { if (isSlot(p)) continue; return false; }
        if (!candidate(colour[p]) || (nodes_[p].forbid & bit(colour[p]))) return false;
        for (int q : nodes_[p].adj) if (colour[q] == colour[p]) return false;
    }
    return true;
}

int Allocator::find(int p) {
    while (rep_[p] != p) p = rep_[p] = rep_[rep_[p]];
    return p;
}

// v's node joins u's: its edges, its forbidden registers, its weight.
void Allocator::merge(int u, int v) {
    for (int w = 0; w < words_; ++w) row(u)[w] |= row(v)[w];
    for (int q = 0; q < n_; ++q) {
        if (!interferes(v, q)) continue;
        row(q)[u / 64] |= 1ull << (u % 64);
        row(q)[v / 64] &= ~(1ull << (v % 64));
    }
    row(u)[u / 64] &= ~(1ull << (u % 64));
    std::fill(row(v), row(v) + words_, 0ull);
    rep_[v] = u;
    physCopies_[u].insert(physCopies_[u].end(), physCopies_[v].begin(), physCopies_[v].end());
    nodes_[u].forbid |= nodes_[v].forbid;
    if (nodes_[u].home < 0 || (nodes_[v].home >= 0 && nodes_[v].weight > nodes_[u].weight)) nodes_[u].home = nodes_[v].home;
    nodes_[u].slot = nodes_[u].slot || nodes_[v].slot;
    nodes_[u].weight += nodes_[v].weight;
    nodes_[u].slotWeight += nodes_[v].slotWeight;
}

// **Briggs' test**: the neighbours of significant degree the two would have
// as one, a forbidden register counting as one each.
int Allocator::significant(int u, int v) const {
    const int k = popcount(palette_);
    int count = popcount((nodes_[u].forbid | nodes_[v].forbid) & palette_);
    for (int q = 0; q < n_; ++q)
        if ((interferes(u, q) || interferes(v, q)) && alive(q) && degree_[q] >= k) ++count;
    return count;
}

// The most a node's copies to and from physical registers can save, all
// of them to one register it may take.
long Allocator::bestPreference(const std::vector<Copy> &prefs, RegSet forbid) const {
    long score[kGprs] = {0}, best = 0;
    for (const Copy &c : prefs) score[isPseudo(c.src) ? c.dst : c.src] += c.weight;
    for (int r = 0; r < kGprs; ++r)
        if (!(forbid & bit(r))) best = std::max(best, score[r]);
    return best;
}

// **The copies are coalesced, heaviest first, where the graph stays
// colourable by Briggs' test, and where the copy saved outweighs the
// copies to physical registers the two nodes could no longer both drop.**
void Allocator::coalesce() {
    const int k = popcount(palette_);
    std::vector<int> byWeight;
    for (std::size_t c = 0; c < copies_.size(); ++c)
        if (isPseudo(copies_[c].src) && isPseudo(copies_[c].dst)) byWeight.push_back(static_cast<int>(c));
    std::stable_sort(byWeight.begin(), byWeight.end(),
                     [&](int x, int y) { return copies_[x].weight > copies_[y].weight; });
    for (int c : byWeight) {
        const int u = find(indexOf(copies_[c].src)), v = find(indexOf(copies_[c].dst));
        if (u == v || interferes(u, v) || significant(u, v) >= k) continue;
        std::vector<Copy> both = physCopies_[u];
        both.insert(both.end(), physCopies_[v].begin(), physCopies_[v].end());
        const long apart = bestPreference(physCopies_[u], nodes_[u].forbid) + bestPreference(physCopies_[v], nodes_[v].forbid);
        if (copies_[c].weight + bestPreference(both, nodes_[u].forbid | nodes_[v].forbid) < apart) continue;
        merge(u, v);
        for (int q = 0; q < n_; ++q) if (rep_[q] == q) degree_[q] = popcount(nodes_[q].forbid & palette_);
        for (int q = 0; q < n_; ++q)
            if (rep_[q] == q)
                for (int w = 0; w < words_; ++w) degree_[q] += popcount(row(q)[w]);
        ++coalesced_;
    }
}

// **Simplify, optimistically**: a node with fewer neighbours than registers
// always colours and goes on the stack; when none is left, the cheapest
// by weight over degree goes anyway, and select finds out.
void Allocator::simplify(std::vector<int> &stack) {
    const int k = popcount(palette_);
    removed_.assign(n_, false);
    for (;;) {
        int p = -1;
        for (int q = 0; q < n_; ++q)
            if (alive(q) && degree_[q] < k) { p = q; break; }
        if (p < 0)
            for (int q = 0; q < n_; ++q)
                if (alive(q) && (p < 0 || nodes_[q].weight * (degree_[p] + 1) < nodes_[p].weight * (degree_[q] + 1))) p = q;
        if (p < 0) return;
        removed_[p] = true;
        stack.push_back(p);
        for (int q = 0; q < n_; ++q) if (interferes(p, q) && alive(q)) --degree_[q];
    }
}

// **The register for a node, its neighbours coloured**: the one its copies
// prefer most, then its home, then the cheapest on offer - a preserved one
// last, and a fresh one only where reserveFresh said so.
int Allocator::pick(int p) {
    const Node &nd = nodes_[p];
    RegSet allowed = palette_ & ~nd.forbid;
    if (nd.home >= 0) allowed |= bit(nd.home) & ~nd.forbid;
    for (int q = 0; q < n_; ++q)
        if (interferes(p, q) && colour_[q] >= 0) allowed &= ~bit(colour_[q]);
    const bool fresh = freshFor_[p];
    long score[kGprs] = {0};
    for (int c : copiesOf_[p]) {
        const int other = isPseudo(copies_[c].src) && find(indexOf(copies_[c].src)) == p ? copies_[c].dst : copies_[c].src;
        const int reg = isPseudo(other) ? colour_[find(indexOf(other))] : other;
        if (reg >= 0 && (allowed & bit(reg))) score[reg] += copies_[c].weight;
    }
    std::vector<int> offer = order_;
    if (nd.home >= 0 && !(palette_ & bit(nd.home))) offer.push_back(nd.home);
    for (int r : kOffer) if ((offered_ & taken_ & bit(r))) offer.push_back(r);
    if (fresh) for (int r : kOffer) if ((offered_ & ~taken_ & bit(r))) offer.push_back(r);
    int best = -1;
    for (int r : offer)
        if ((allowed & bit(r)) && (best < 0 || score[r] > score[best] || (score[r] == score[best] && r == nd.home)))
            best = r;
    if (best >= 0 && (offered_ & bit(best))) taken_ |= bit(best);
    return best;
}

// **The level's preserved registers go to the locals that need one most**:
// a node no caller-saved register can hold, its accesses earning the save,
// the heaviest first up to the level's count.
void Allocator::reserveFresh() {
    freshFor_.assign(n_, false);
    std::vector<int> needy;
    for (int p = 0; p < n_; ++p)
        if (alive(p) && nodes_[p].slot && (palette_ & ~offered_ & ~nodes_[p].forbid) == 0 &&
            nodes_[p].slotWeight >= fn_.costs().minWeight())
            needy.push_back(p);
    std::stable_sort(needy.begin(), needy.end(),
                     [&](int x, int y) { return nodes_[x].slotWeight > nodes_[y].slotWeight; });
    for (std::size_t i = 0; i < needy.size() && static_cast<int>(i) < fn_.costs().registers(); ++i) freshFor_[needy[i]] = true;
}

// The node select could not colour, or -1.
int Allocator::select(const std::vector<int> &stack) {
    colour_.assign(n_, -1);
    for (int i = static_cast<int>(stack.size()) - 1; i >= 0; --i) {
        const int p = stack[i];
        const int reg = pick(p);
        if (reg < 0) return p;
        colour_[p] = reg;
    }
    for (int p = 0; p < n_; ++p)
        if (!demoted_[p]) colour_[p] = colour_[find(p)];
    return -1;
}

// Chaitin-Briggs over the graph; the node left without a register, or -1.
int Allocator::colour() {
    palette_ = 0;
    order_.clear();
    taken_ = 0;
    for (int r : kOffer)
        if (candidate(r) && !(fn_.convention.preserved & bit(r))) { palette_ |= bit(r); order_.push_back(r); }
    palette_ |= offered_;
    rep_.resize(n_);
    for (int p = 0; p < n_; ++p) rep_[p] = p;
    removed_.assign(n_, false);
    degree_.assign(n_, 0);
    for (int p = 0; p < n_; ++p)
        degree_[p] = static_cast<int>(nodes_[p].adj.size()) + popcount(nodes_[p].forbid & palette_);
    physCopies_.assign(n_, std::vector<Copy>());
    for (const Copy &c : copies_)
        if (isPseudo(c.src) != isPseudo(c.dst)) physCopies_[indexOf(isPseudo(c.src) ? c.src : c.dst)].push_back(c);
    coalesced_ = 0;
    coalesce();
    reserveFresh();
    copiesOf_.assign(n_, std::vector<int>());
    for (std::size_t c = 0; c < copies_.size(); ++c) {
        const int u = isPseudo(copies_[c].src) ? find(indexOf(copies_[c].src)) : -1;
        const int v = isPseudo(copies_[c].dst) ? find(indexOf(copies_[c].dst)) : -1;
        if (u >= 0 && u != v) copiesOf_[u].push_back(static_cast<int>(c));
        if (v >= 0 && u != v) copiesOf_[v].push_back(static_cast<int>(c));
    }
    std::vector<int> stack;
    simplify(stack);
    const int failed = select(stack);
    assert((failed >= 0 || valid(colour_)) && "a colouring select made must honour the graph");
    return failed;
}

// **The promoted locals the failed node holds go back to their slots**, in
// the stream, so the graph is built again without them; false where it
// holds none, which only the homes can answer.
bool Allocator::demote(int failed) {
    std::vector<int> back(n_, Webs::kAsIs);
    bool any = false;
    for (int q = nHomes_; q < n_; ++q)
        if (!demoted_[q] && find(q) == failed) { demoted_[q] = true; back[q] = Webs::kToSlot; any = true; }
    if (any) Webs::assign(fn_, back);
    return any;
}

// **A preserved register taken is saved by the prologue and restored before
// each return**, its slot below the frame after the saves already made.
void Allocator::addSaves() {
    RegSet used = 0;
    for (int p = 0; p < n_; ++p) if (colour_[p] >= 0) used |= bit(colour_[p]);
    std::vector<SavedReg> added;
    for (int r : kOffer)
        if (used & offered_ & bit(r))
            added.push_back(SavedReg{regName(r, 8), -(fn_.frameBase() + 8 * static_cast<long long>(fn_.saves.size() + added.size() + 1))});
    if (added.empty()) return;
    insertRestores(fn_.stream, added);
    fn_.saves.insert(fn_.saves.end(), added.begin(), added.end());
}

bool Allocator::run() {
    coalesced_ = 0;
    fellBack_ = false;
    if (n_ == 0) return Webs::dropSelfCopies(fn_);
    demoted_.assign(n_, false);
    for (;;) {
        findUses();
        solveLiveness();
        buildInterference();
        collectCosts();
        std::vector<int> homes(n_, Webs::kToSlot);
        for (int p = 0; p < nHomes_; ++p) homes[p] = homes_[p];
        assert(valid(homes) && "the homes must colour the graph: they are how the stream ran");
        const int failed = colour();
        if (failed < 0) break;
        if (demote(failed)) continue;
        colour_ = homes;
        coalesced_ = 0;
        fellBack_ = true;
        break;
    }
    bool moved = false;
    for (int p = 0; p < nHomes_; ++p) moved = moved || colour_[p] != homes_[p];
    for (int p = nHomes_; p < n_; ++p) moved = moved || colour_[p] >= 0;
    addSaves();
    Webs::assign(fn_, colour_);
    const bool dropped = Webs::dropSelfCopies(fn_);
    return moved || dropped;
}

}
