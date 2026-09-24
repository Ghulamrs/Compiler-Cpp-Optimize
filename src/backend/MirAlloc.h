#pragma once

// **The register allocator** - GCC's IRA reduced to one function with no
// regions: interference from liveness over the pseudos, Chaitin-Briggs
// colouring with the copies as preferences, costs from the level.

#include "Mir.h"

#include <vector>

namespace mir {

// **Every pseudo given a register, and the copies that then copy a register
// to itself dropped.** The homes - where each was found - are a colouring
// known to work: the answer where no better is found, and the graph's check.
class Allocator {
public:
    Allocator(opt::Function &fn, const std::vector<int> &homes);

    // Colours and writes the registers back; whether the stream changed.
    bool run();
    // How many copies coalesced away, and whether the homes had to serve.
    int coalesced() const { return coalesced_; }
    bool fellBack() const { return fellBack_; }

private:
    // A pseudo's set, one bit each; the words a function's pseudos take.
    typedef std::vector<unsigned long long> Bits;
    // What one instruction does with one pseudo.
    struct Use {
        int pseudo;
        bool read;
        bool write;
    };
    // A whole copy between two registers, either of them a pseudo: a preference.
    struct Copy {
        int entry;
        int src, dst;               // a pseudo (>= kFirstPseudo) or a physical register
        long weight;
    };
    // One pseudo's place in the graph.
    struct Node {
        std::vector<int> adj;       // the pseudos it interferes with
        opt::RegSet forbid = 0;     // the physical registers it may not take
        long weight = 0;            // its references, weighted by the level
        int home = 0;
    };

    opt::Function &fn_;
    const std::vector<int> &homes_;
    int n_ = 0;
    int words_ = 0;
    std::vector<std::vector<Use>> uses_;    // per entry
    std::vector<Bits> liveIn_, liveOut_;    // per block
    std::vector<Node> nodes_;
    Bits matrix_;                           // n_ by n_: whether two pseudos interfere
    std::vector<Copy> copies_;
    std::vector<int> colour_;
    int coalesced_ = 0;
    bool fellBack_ = false;
    // The colouring's state: the registers on offer, each node's
    // representative once coalesced, its degree, and its copies.
    opt::RegSet palette_ = 0;
    std::vector<int> order_;
    std::vector<int> rep_;
    std::vector<int> degree_;
    std::vector<bool> removed_;
    std::vector<std::vector<int>> copiesOf_;
    std::vector<std::vector<Copy>> physCopies_;   // per node: its copies to or from a physical register

    void findUses();
    void solveLiveness();
    void buildInterference();
    void collectCosts();
    bool valid(const std::vector<int> &colour) const;
    bool colour();
    void coalesce();
    void simplify(std::vector<int> &stack);
    bool select(const std::vector<int> &stack);
    int find(int p);
    void merge(int u, int v);
    int significant(int u, int v) const;
    long bestPreference(const std::vector<Copy> &prefs, opt::RegSet forbid) const;
    int pick(int p);

    bool interferes(int p, int q) const { return (matrix_[static_cast<std::size_t>(p) * words_ + q / 64] >> (q % 64)) & 1; }
    unsigned long long *row(int p) { return &matrix_[static_cast<std::size_t>(p) * words_]; }
    const unsigned long long *row(int p) const { return &matrix_[static_cast<std::size_t>(p) * words_]; }
    void addEdge(int p, int q);
    bool alive(int p) const { return rep_[p] == p && !removed_[p]; }
    static void set(Bits &b, int p) { b[p / 64] |= 1ull << (p % 64); }
    static void clear(Bits &b, int p) { b[p / 64] &= ~(1ull << (p % 64)); }
    static bool has(const Bits &b, int p) { return (b[p / 64] >> (p % 64)) & 1; }
};

}
