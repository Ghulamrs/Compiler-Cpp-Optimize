#include "Inliner.h"

#include <algorithm>

namespace {

// **A body's size is its node count**, every expression and statement one; and each call in
// it is noted with the loops it sits inside. The unit of GCC's inline parameters is its own
// estimate of instructions, and a node here is near enough one to take its numbers as a start.
class Measurer final : public Visitor {
public:
    int nodes = 0;
    bool cleanups = false;                          // a block that runs destructors on unwind
    bool labels = false;                            // a goto's label or a case's: the parser names them once per function
    std::vector<std::pair<const Call *, int>> calls; // each call, and its loop depth

    void visit(const Num &) override { ++nodes; }
    void visit(const Var &) override { ++nodes; }
    void visit(const StrLit &) override { ++nodes; }
    void visit(const Goto &) override { ++nodes; labels = true; }
    void visit(const Break &) override { ++nodes; }
    void visit(const Continue &) override { ++nodes; }
    void visit(const FuncletLeave &) override { ++nodes; labels = true; }
    void visit(const Call &n) override {
        ++nodes;
        calls.push_back(std::make_pair(&n, depth_));
        if (n.callee() != nullptr) n.callee()->accept(*this);
        for (const ExprPtr &a : n.args()) a->accept(*this);
    }
    void visit(const VaStart &n) override { ++nodes; n.list().accept(*this); }
    void visit(const VaArg &n) override { ++nodes; n.list().accept(*this); }
    void visit(const Assign &n) override { ++nodes; n.target().accept(*this); n.value().accept(*this); }
    void visit(const Unary &n) override { ++nodes; n.operand().accept(*this); }
    void visit(const Binary &n) override { ++nodes; n.lhs().accept(*this); n.rhs().accept(*this); }
    void visit(const Postfix &n) override { ++nodes; n.target().accept(*this); }
    void visit(const Cast &n) override { ++nodes; n.value().accept(*this); }
    void visit(const Comma &n) override { ++nodes; n.left().accept(*this); n.right().accept(*this); }
    void visit(const Conditional &n) override {
        ++nodes;
        n.cond().accept(*this); n.thenArm().accept(*this); n.elseArm().accept(*this);
    }
    void visit(const MemberAccess &n) override { ++nodes; n.object().accept(*this); }
    void visit(const ExprStmt &n) override { ++nodes; n.expr().accept(*this); }
    void visit(const Return &n) override { ++nodes; if (n.hasValue()) n.value().accept(*this); }
    void visit(const Block &n) override {
        ++nodes;
        if (n.unwindCleanup()) cleanups = true;
        for (const StmtPtr &s : n.body()) s->accept(*this);
    }
    void visit(const If &n) override {
        ++nodes;
        n.cond().accept(*this); n.thenArm().accept(*this);
        if (n.elseArm() != nullptr) n.elseArm()->accept(*this);
    }
    void visit(const While &n) override { ++nodes; n.cond().accept(*this); inLoop(n.body()); }
    void visit(const DoWhile &n) override { ++nodes; inLoop(n.body()); n.cond().accept(*this); }
    void visit(const For &n) override {
        ++nodes;
        if (n.init() != nullptr) n.init()->accept(*this);
        if (n.cond() != nullptr) n.cond()->accept(*this);
        if (n.step() != nullptr) n.step()->accept(*this);
        inLoop(n.body());
    }
    void visit(const Switch &n) override { ++nodes; labels = true; n.cond().accept(*this); n.body().accept(*this); }
    void visit(const Case &n) override { ++nodes; n.body().accept(*this); }
    void visit(const Label &n) override { ++nodes; labels = true; n.body().accept(*this); }
    void visit(const Try &n) override {
        ++nodes;
        for (const StmtPtr &s : n.body()) s->accept(*this);
        if (n.hasPad()) n.pad().accept(*this);
        if (n.cleanup() != nullptr) n.cleanup()->accept(*this);
        for (const MsHandler &h : n.handlers())
            if (h.body != nullptr) h.body->accept(*this);
    }

private:
    int depth_ = 0;
    void inLoop(const Node &body) { ++depth_; body.accept(*this); --depth_; }
};

// What a call sequence costs, in the same measure: the call itself and the moves that place its arguments.
int callCost(const Call &site) { return 1 + static_cast<int>(site.args().size()); }

int roundedFrame(const Function &fn) { return (fn.frameSize() + 15) & ~15; }

}

Inliner::Inliner(const opt::Costs &costs) : costs_(costs) {}

// **Measure, then decide every site in one pass, the best first.** A site
// the budgets admit is charged to its caller and to the unit at once, so a
// worse site later finds what is left.
void Inliner::summarize(const Program &program) {
    std::vector<Site> sites = sitesOf(program);
    sortByBadness(sites);
    for (const Site &site : sites) {
        const bool yes = withinBudgets(site);
        decided_[site.call] = yes;
        if (!yes) continue;
        charge(site);
        largestFrame_ = std::max(largestFrame_, roundedFrame(*site.callee));
    }
}

// Every function measured, and every call of one this unit defines, a
// call of itself included, listed with its caller and its growth.
std::vector<Inliner::Site> Inliner::sitesOf(const Program &program) {
    std::map<std::string, const Function *> bySymbol;
    std::vector<std::pair<const Function *, Measurer>> measured;
    for (const Function &fn : program.functions) {
        measured.emplace_back(&fn, Measurer());
        Measurer &m = measured.back().second;
        fn.body().accept(m);
        Measure &measure = measures_[&fn];
        measure.size = m.nodes;
        measure.eligible = !fn.hasLandingPads() && !fn.isVariadic() && fn.regSaveSlot() == 0 && !m.cleanups && !m.labels;
        unitSize_ += m.nodes;
        bySymbol[fn.symbol()] = &fn;
    }
    std::vector<Site> sites;
    for (const auto &fm : measured)
        for (const auto &call : fm.second.calls) {
            const auto callee = bySymbol.find(call.first->symbol());
            if (callee == bySymbol.end() || !eligible(*callee->second)) continue;
            const int growth = measures_[callee->second].size - callCost(*call.first);
            sites.push_back(Site{call.first, fm.first, callee->second, call.second, growth});
        }
    return sites;
}

bool Inliner::eligible(const Function &callee) const {
    const auto it = measures_.find(&callee);
    return it != measures_.end() && it->second.eligible;
}

bool Inliner::allows(const Call &site) const {
    const auto it = decided_.find(&site);
    return it != decided_.end() && it->second;
}

// **GCC's badness, reduced**: growth over how often the site runs, with
// each loop level worth twice the one outside it. Sorted stably, so equal
// sites keep program order.
void Inliner::sortByBadness(std::vector<Site> &sites) {
    std::stable_sort(sites.begin(), sites.end(), [](const Site &x, const Site &y) {
        return static_cast<long>(x.growth) << y.loopDepth < static_cast<long>(y.growth) << x.loopDepth;
    });
}

// **Three budgets, all of which must hold**: the site's growth against what a site this deep
// in loops may take; the caller's growth so far, with this one, against its own size; the
// unit's likewise. A budget of zero percent still admits a site that grows nothing.
bool Inliner::withinBudgets(const Site &site) const {
    const int growth = site.growth;
    if (growth > costs_.inlineGrowth(site.loopDepth)) return false;
    if (growth <= 0) return true;
    const Measure &c = measures_.at(site.caller);
    const long callerRoom = static_cast<long>(std::max(c.size, costs_.largeFunction())) * costs_.callerGrowthPercent() / 100;
    const long unitRoom = static_cast<long>(unitSize_) * costs_.unitGrowthPercent() / 100;
    return c.taken + growth <= callerRoom && unitTaken_ + growth <= unitRoom;
}

void Inliner::charge(const Site &site) {
    if (site.growth <= 0) return;
    measures_[site.caller].taken += site.growth;
    unitTaken_ += site.growth;
}
