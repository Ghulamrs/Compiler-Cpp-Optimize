#pragma once

// **The register allocator** - GCC's IRA reduced to one function with no
// regions: interference from liveness over the pseudos, Chaitin-Briggs
// colouring with the copies as preferences, costs from the level.

#include "Mir.h"

#include <vector>

namespace mir {

// **Every pseudo given a register, and the copies that then copy a register
// to itself dropped.** A web's home is a colouring known to work - the
// answer where no better is found, and the graph's check; a local has its slot.
class Allocator {
public:
    explicit Allocator(opt::Function &fn);

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
        long slotWeight = 0;        // a promoted local's accesses, in minWeight's unit
        int home = -1;              // a web's register; a promoted local has none
        bool slot = false;          // holds a promoted local, whose slot could serve
    };

    opt::Function &fn_;
    const std::vector<int> &homes_;
    int nHomes_ = 0;                        // the webs' pseudos; the promoted locals' follow
    int n_ = 0;
    int words_ = 0;
    std::vector<std::vector<Use>> uses_;    // per entry
    std::vector<Bits> liveIn_, liveOut_;    // per block
    std::vector<Node> nodes_;
    Bits matrix_;                           // n_ by n_: whether two pseudos interfere
    std::vector<Copy> copies_;
    std::vector<int> colour_;
    std::vector<bool> demoted_;             // a promoted local given its slot back
    int coalesced_ = 0;
    bool fellBack_ = false;
    // The colouring's state: the registers on offer - the caller-saved
    // ones, and the preserved ones a save can be charged for - each node's
    // representative once coalesced, its degree, and its copies.
    opt::RegSet palette_ = 0;
    opt::RegSet offered_ = 0;
    opt::RegSet taken_ = 0;
    std::vector<int> order_;
    std::vector<int> rep_;
    std::vector<int> degree_;
    std::vector<bool> removed_;
    std::vector<bool> freshFor_;            // may take a preserved register nobody has yet
    std::vector<std::vector<int>> copiesOf_;
    std::vector<std::vector<Copy>> physCopies_;   // per node: its copies to or from a physical register

    void findUses();
    void solveLiveness();
    void buildInterference();
    void collectCosts();
    bool valid(const std::vector<int> &colour) const;
    int colour();
    void coalesce();
    void simplify(std::vector<int> &stack);
    int select(const std::vector<int> &stack);
    int find(int p);
    void merge(int u, int v);
    int significant(int u, int v) const;
    long bestPreference(const std::vector<Copy> &prefs, opt::RegSet forbid) const;
    int pick(int p);
    void reserveFresh();
    bool demote(int failed);
    void addSaves();

    bool isSlot(int p) const { return p >= nHomes_; }
    bool interferes(int p, int q) const { return (matrix_[static_cast<std::size_t>(p) * words_ + q / 64] >> (q % 64)) & 1; }
    unsigned long long *row(int p) { return &matrix_[static_cast<std::size_t>(p) * words_]; }
    const unsigned long long *row(int p) const { return &matrix_[static_cast<std::size_t>(p) * words_]; }
    void addEdge(int p, int q);
    bool alive(int p) const { return rep_[p] == p && !removed_[p] && !demoted_[p]; }
    static void set(Bits &b, int p) { b[p / 64] |= 1ull << (p % 64); }
    static void clear(Bits &b, int p) { b[p / 64] &= ~(1ull << (p % 64)); }
    static bool has(const Bits &b, int p) { return (b[p / 64] >> (p % 64)) & 1; }
};

}
