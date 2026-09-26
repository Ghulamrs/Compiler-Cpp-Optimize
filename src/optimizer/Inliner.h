#pragma once

// **Which calls are walked in place of a call instruction.** GCC's ipa-inline reduced to what the walker can know before it walks: every function of the unit measured once, every call site judged in one pass over the unit - the best sites first, as GCC's badness queue orders them - against three budgets the level's costs set, per site, per caller and per unit; then the walker asks about each site as it reaches it and gets the decision already made.
// What makes a callee safe to walk in place at all (no landing pad, no cleanup to run on unwind, no goto label and no switch - the parser names their labels once per function, and a body walked twice would name them twice - not variadic, no register save area) is decided here too; what makes a site safe (a direct call, no stack arguments, not inside another callee walked in place) only the walker knows, and it asks before this is asked.
// The long form is in CLAUDE.md, "The optimizer's design notes".

#include "../Ast.h"
#include "OptCosts.h"

#include <map>
#include <vector>

class Inliner {
public:
    explicit Inliner(const opt::Costs &costs);

    // Every function of the unit measured and every site decided, before any is walked.
    void summarize(const Program &program);

    // Whether the callee may be walked in place anywhere.
    bool eligible(const Function &callee) const;

    // Whether this call is walked in place.
    bool allows(const Call &site) const;

    // The largest frame any site of the unit brings in, for a caller whose frame must be fixed before its body is walked.
    int largestFrame() const { return largestFrame_; }

private:
    // What is known of one function: its body's size in AST nodes, and the
    // growth its own sites have taken so far.
    struct Measure {
        int size = 0;
        bool eligible = false;
        int taken = 0;
    };
    // One call of a function this unit defines: the loops it sits in, and
    // what walking the callee in its place adds to the caller, in nodes.
    struct Site {
        const Call *call;
        const Function *caller;
        const Function *callee;
        int loopDepth;
        int growth;
    };

    const opt::Costs &costs_;
    std::map<const Function *, Measure> measures_;
    std::map<const Call *, bool> decided_;
    int unitSize_ = 0;
    int unitTaken_ = 0;
    int largestFrame_ = 0;

    // Every function measured, every site of one this unit defines listed.
    std::vector<Site> sitesOf(const Program &program);
    // Sites in the order they are worth taking: the least growth for the deepest loop first, ties in program order.
    static void sortByBadness(std::vector<Site> &sites);
    // Whether the site's growth is within what a site this deep in loops may take, and the caller and the unit have the room.
    bool withinBudgets(const Site &site) const;
    void charge(const Site &site);
};
