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

int popcount(unsigned long long x) {
    int n = 0;
    for (; x; x &= x - 1) ++n;
    return n;
}

// The registers offered, cheapest encoding first: the frame's never, and
// the preserved ones not until a save can be charged for.
const int kOffer[] = {RAX, RCX, RDX, RSI, RDI, R8, R9, R10, R11,
                      RBX, R12, R13, R14, R15};

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
    if (nodes_[v].weight > nodes_[u].weight) nodes_[u].home = nodes_[v].home;
    nodes_[u].weight += nodes_[v].weight;
}

// **Briggs' test**: the neighbours of significant degree the two would have
// as one, a forbidden register counting as one each.
int Allocator::significant(int u, int v) const {
    const int k = popcount(palette_);
    int count = popcount((nodes_[u].forbid | nodes_[v].forbid) & palette_);
    for (int q = 0; q < n_; ++q)
        if ((interferes(u, q) || interferes(v, q)) && rep_[q] == q && degree_[q] >= k) ++count;
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
// prefer most, by weight, then its home, then the cheapest on offer; -1
// when every one is taken.
int Allocator::pick(int p) {
    RegSet allowed = (palette_ | bit(nodes_[p].home)) & ~nodes_[p].forbid;
    for (int q = 0; q < n_; ++q)
        if (interferes(p, q) && colour_[q] >= 0) allowed &= ~bit(colour_[q]);
    long score[kGprs] = {0};
    for (int c : copiesOf_[p]) {
        const int other = isPseudo(copies_[c].src) && find(indexOf(copies_[c].src)) == p ? copies_[c].dst : copies_[c].src;
        const int reg = isPseudo(other) ? colour_[find(indexOf(other))] : other;
        if (reg >= 0 && (allowed & bit(reg))) score[reg] += copies_[c].weight;
    }
    int best = -1;
    std::vector<int> offer = order_;
    if (!(palette_ & bit(nodes_[p].home))) offer.push_back(nodes_[p].home);
    for (int r : offer)
        if ((allowed & bit(r)) && (best < 0 || score[r] > score[best] || (score[r] == score[best] && r == nodes_[p].home)))
            best = r;
    return best;
}

bool Allocator::select(const std::vector<int> &stack) {
    colour_.assign(n_, -1);
    for (int i = static_cast<int>(stack.size()) - 1; i >= 0; --i) {
        const int p = stack[i];
        const int reg = pick(p);
        if (reg < 0) return false;
        colour_[p] = reg;
    }
    for (int p = 0; p < n_; ++p) colour_[p] = colour_[find(p)];
    return true;
}

// Chaitin-Briggs over the graph; the homes if a pseudo ends without a register.
bool Allocator::colour() {
    palette_ = 0;
    order_.clear();
    for (int r : kOffer)
        if (candidate(r) && !(fn_.convention.preserved & bit(r))) { palette_ |= bit(r); order_.push_back(r); }
    rep_.resize(n_);
    for (int p = 0; p < n_; ++p) rep_[p] = p;
    degree_.assign(n_, 0);
    for (int p = 0; p < n_; ++p)
        degree_[p] = static_cast<int>(nodes_[p].adj.size()) + popcount(nodes_[p].forbid & palette_);
    physCopies_.assign(n_, std::vector<Copy>());
    for (const Copy &c : copies_)
        if (isPseudo(c.src) != isPseudo(c.dst)) physCopies_[indexOf(isPseudo(c.src) ? c.src : c.dst)].push_back(c);
    coalesce();
    copiesOf_.assign(n_, std::vector<int>());
    for (std::size_t c = 0; c < copies_.size(); ++c) {
        const int u = isPseudo(copies_[c].src) ? find(indexOf(copies_[c].src)) : -1;
        const int v = isPseudo(copies_[c].dst) ? find(indexOf(copies_[c].dst)) : -1;
        if (u >= 0 && u != v) copiesOf_[u].push_back(static_cast<int>(c));
        if (v >= 0 && u != v) copiesOf_[v].push_back(static_cast<int>(c));
    }
    std::vector<int> stack;
    simplify(stack);
    if (!select(stack)) {
        colour_ = homes_;
        coalesced_ = 0;
        return false;
    }
    assert(valid(colour_) && "a colouring select made must honour the graph");
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
    assert(valid(homes_) && "the homes must colour the graph: they are how the stream ran");
    fellBack_ = !colour();
    bool moved = false;
    for (int p = 0; p < n_; ++p) moved = moved || colour_[p] != homes_[p];
    Webs::assign(fn_, colour_);
    const bool dropped = Webs::dropSelfCopies(fn_);
    return moved || dropped;
}

}
