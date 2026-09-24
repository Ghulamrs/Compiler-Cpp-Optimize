#pragma once

// **The pass manager**, after GCC's: a pass has a name, a gate, an execute,
// the properties it requires, provides and destroys, and the work the
// manager does before it runs; a group is a pass with sub-passes, run
// while its gate holds and repeated by its policy; the pipeline is one
// declaration of the tree (OptPipeline.cpp), and a dump after any named
// pass shows the stream as that pass left it.

#include "OptFunction.h"

#include <memory>
#include <vector>

namespace opt {

// **Work the manager does before a pass**, GCC's TODO flags: drop the labels
// only jumps named that nothing names now, and build the flow graph.
enum Todo : unsigned {
    kTodoDropLabels = 1u << 0,
    kTodoBuildFlow = 1u << 1,
};

struct PassInfo {
    const char *name;
    unsigned required;      // properties the pass needs; checked before it runs
    unsigned provided;      // properties true after it
    unsigned destroyed;     // properties no longer true after it
    unsigned todoStart;     // done before it, every time it runs
};

class Pass {
public:
    explicit Pass(const PassInfo &info) : info_(info) {}
    virtual ~Pass() {}
    const PassInfo &info() const { return info_; }
    // Whether the pass runs on this function at all.
    virtual bool gate(const Function &) const { return true; }
    // Does the work; says whether anything changed, which the manager takes
    // as "solve the liveness again" and a group as "go round again".
    virtual bool execute(Function &fn) = 0;

private:
    PassInfo info_;
};

// **A pass made of passes.** Run its sub-passes in order, `repeat` times
// at most (0: the level's round count), and stop early by the policy:
// when a whole round changed nothing, or as soon as the first sub-pass of
// a round changed nothing - the shape of "shrink, and if that found
// something, everything again".
class Group : public Pass {
public:
    enum Stop { WhenNoneChanged, WhenFirstUnchanged };
    Group(const PassInfo &info, int repeat, Stop stop) : Pass(info), repeat_(repeat), stop_(stop) {}
    Group &add(std::unique_ptr<Pass> p) { subs_.push_back(std::move(p)); return *this; }
    bool execute(Function &) override { return false; }     // the manager runs the subs
    int repeat(const Function &fn) const { return repeat_ > 0 ? repeat_ : fn.costs.rounds; }
    Stop stop() const { return stop_; }
    const std::vector<std::unique_ptr<Pass>> &subs() const { return subs_; }

private:
    std::vector<std::unique_ptr<Pass>> subs_;
    int repeat_;
    Stop stop_;
};

// **Runs a pass tree over a function.** For each pass whose gate holds: the
// start work, a check of what it requires, execute, the property update,
// the liveness marked stale if it changed, and the dump if asked for.
//
// CXX1_DUMP_MIR names the passes to dump after, comma-separated, or `all`;
// the dump goes to stderr, one section per pass per function.
class PassManager {
public:
    PassManager();
    bool run(Pass &pass, Function &fn);

private:
    std::vector<std::string> dump_;
    bool dumpAll_ = false;

    bool runLeaf(Pass &pass, Function &fn);
    bool runGroup(Group &group, Function &fn);
    void before(const Pass &pass, Function &fn);
    void after(const Pass &pass, Function &fn, bool changed);
    bool wanted(const Pass &pass) const;
};

// The labels only jumps name, that no jump names any more, marked dead:
// each joins its block to the one before, so what is known flows through.
void dropUnnamedLabels(Function &fn);

// One function's stream as text, for the dumps.
void dumpStream(std::ostream &o, const Function &fn);

}
