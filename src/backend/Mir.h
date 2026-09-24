#pragma once

// **The IR's registers** - see docs/OPTIMIZER-IR.md. A pseudo register is
// numbered above every physical one; the passes before allocation see pseudos
// wherever nothing pins a value to the register it happens to be in.

#include "OptFunction.h"

#include <vector>

namespace mir {

constexpr int kFirstPseudo = opt::kPhysical;
inline bool isPseudo(int id) { return id >= kFirstPseudo; }

// **A web is one value's life in one register**: every definition joined
// with every use it reaches, over reaching definitions, as GCC's web pass
// makes them. Those the ABI does not pin become pseudos; the register each
// was found in is kept as its home, which is what it is given back until an
// allocator chooses better.
//
// An instruction that wants a value in one register - a call its arguments,
// a shift its count in %cl, `idiv` its dividend in rdx:rax - or that leaves
// one there - a call its result in rax - would pin the whole web the value
// belongs to. Split first: the value is copied into or out of that register
// just there, so only the copy is pinned and the web that computed it is
// free. That is how GCC's expander places every fixed-register operand, and
// its allocator coalesces the copies it can.
class Webs {
public:
    explicit Webs(opt::Function &fn);

    // **A pinned occurrence becomes a copy.** Inserts the copies, rebuilds the
    // flow, and says whether it inserted any.
    bool splitPinned();
    // Every unpinned web renamed to a pseudo; needs the flow to describe the stream.
    void build();
    // The register each pseudo was found in.
    const std::vector<int> &homes() const { return home_; }
    int count() const { return static_cast<int>(home_.size()); }
    // How many webs the instructions pinned, of those built.
    int pinned() const { return pinned_; }

    // Every pseudo named in the stream given its register: `colour[p - kFirstPseudo]`.
    static void assign(opt::Function &fn, const std::vector<int> &colour);
    // A whole copy of a register to itself, which an assignment leaves where
    // a split was made or a copy coalesced, goes; a four-byte one
    // zero-extends and stays.
    static bool dropSelfCopies(opt::Function &fn);

private:
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
    // Union-find over definitions; a pin on any member pins the web.
    class Sets {
    public:
        int make(bool pin);
        int find(int x);
        void unite(int x, int y);
        void pin(int x) { pinned_[find(x)] = true; }
        bool isPinned(int root) const { return pinned_[root]; }
        int size() const { return static_cast<int>(parent_.size()); }
    private:
        std::vector<int> parent_;
        std::vector<bool> pinned_;
    };

    opt::Function &fn_;
    std::vector<int> home_;
    int pinned_ = 0;

    // The state of one build, phase by phase.
    Sets sets_;
    std::vector<std::vector<Occurrence>> occ_;          // per entry
    std::vector<std::vector<int>> defAt_;               // per entry, per operand: its definition
    std::vector<std::vector<int>> implicitDef_;         // per entry, per register: the unnamed definition
    std::vector<std::vector<int>> useWeb_;              // per entry, per operand: the web it uses
    std::vector<std::vector<int>> unknownIn_;           // per block, per register: the value it starts with

    static std::vector<Occurrence> occurrencesOf(const opt::Instr &i, int entry);
    // Registers an instruction reads or writes without naming them.
    opt::RegSet implicitReads(int k) const;
    opt::RegSet implicitWrites(int k) const;
    void makeDefinitions();
    void joinUsesToDefinitions();
    void renameToPseudos();
};

}
