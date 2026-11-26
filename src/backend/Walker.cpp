#include "Walker.h"

#include <set>

#include <algorithm>

#include "../Source.h"

namespace {
// An lvalue of volatile type - a variable, a dereference, a member - read or written here.
class VolatileScan final : public Visitor {
public:
    bool seen = false;
    bool alloca = false;     // an `_alloca` (Call::isAlloca)
    bool catches = false;    // a `try` with a handler: Microsoft's catch, or an Itanium row with types
    void note(const Expr &n) { if (n.type() != nullptr && n.type()->isVolatile()) seen = true; }
    void visit(const Num &) override {}
    void visit(const Var &n) override { note(n); }
    void visit(const StrLit &) override {}
    void visit(const Goto &) override {}
    void visit(const Break &) override {}
    void visit(const Continue &) override {}
    void visit(const FuncletLeave &) override {}
    void visit(const Call &n) override {
        if (n.isAlloca()) alloca = true;
        if (n.callee() != nullptr) n.callee()->accept(*this);
        for (const ExprPtr &a : n.args()) a->accept(*this);
    }
    void visit(const VaStart &n) override { n.list().accept(*this); }
    void visit(const VaArg &n) override { n.list().accept(*this); }
    void visit(const Assign &n) override { n.target().accept(*this); n.value().accept(*this); }
    void visit(const Unary &n) override { note(n); n.operand().accept(*this); }
    void visit(const Binary &n) override { n.lhs().accept(*this); n.rhs().accept(*this); }
    void visit(const Postfix &n) override { n.target().accept(*this); }
    void visit(const Cast &n) override { n.value().accept(*this); }
    void visit(const Comma &n) override { n.left().accept(*this); n.right().accept(*this); }
    void visit(const Conditional &n) override {
        n.cond().accept(*this); n.thenArm().accept(*this); n.elseArm().accept(*this);
    }
    void visit(const MemberAccess &n) override { note(n); n.object().accept(*this); }
    void visit(const ExprStmt &n) override { n.expr().accept(*this); }
    void visit(const Return &n) override { if (n.hasValue()) n.value().accept(*this); }
    void visit(const Block &n) override { for (const StmtPtr &s : n.body()) s->accept(*this); }
    void visit(const If &n) override {
        n.cond().accept(*this); n.thenArm().accept(*this);
        if (n.elseArm() != nullptr) n.elseArm()->accept(*this);
    }
    void visit(const While &n) override { n.cond().accept(*this); n.body().accept(*this); }
    void visit(const DoWhile &n) override { n.body().accept(*this); n.cond().accept(*this); }
    void visit(const For &n) override {
        if (n.init() != nullptr) n.init()->accept(*this);
        if (n.cond() != nullptr) n.cond()->accept(*this);
        if (n.step() != nullptr) n.step()->accept(*this);
        n.body().accept(*this);
    }
    void visit(const Switch &n) override { n.cond().accept(*this); n.body().accept(*this); }
    void visit(const Case &n) override { n.body().accept(*this); }
    void visit(const Label &n) override { n.body().accept(*this); }
    void visit(const Try &n) override {
        if (!n.handlers().empty() || !n.typeIndices().empty()) catches = true;
        for (const StmtPtr &s : n.body()) s->accept(*this);
        if (n.hasPad()) n.pad().accept(*this);
        if (n.cleanup() != nullptr) n.cleanup()->accept(*this);
        for (const MsHandler &h : n.handlers())
            if (h.body != nullptr) h.body->accept(*this);
    }
};
}

bool touchesVolatile(const Node &n) {
    VolatileScan scan;
    n.accept(scan);
    return scan.seen;
}

bool callsAlloca(const Node &n) {
    VolatileScan scan;
    n.accept(scan);
    return scan.alloca;
}

bool catchesHere(const Node &n) {
    VolatileScan scan;
    n.accept(scan);
    return scan.catches;
}

void Walker::markLine(const Stmt &n) { markLine(n.pos()); }

void Walker::markLine(std::size_t pos) {
    if (lines_ == nullptr) return;

    if (pos == 0) return;
    Source::Place at = lines_->locate(pos);
    std::size_t before = emittedSize();
    emitLoc(at.file + 1, at.line, at.column);

    notCode_ += emittedSize() - before;
}

std::size_t Walker::openingBrace(std::size_t pos) const {
    if (lines_ == nullptr) return pos;
    const std::string &text = lines_->text();
    int depth = 0;
    for (std::size_t at = pos; at < text.size(); at++) {
        const char c = text[at];
        if (c == '(') depth++;
        else if (c == ')') depth--;
        else if (c == ';' && depth == 0) return pos;
        else if (c == '{' && depth == 0) return at;
    }
    return pos;
}

void Walker::visit(const ExprStmt &n) { markLine(n); n.expr().accept(*this); }

void Walker::resetBlocks(const std::vector<int> &parents) {
    blocks_.clear();
    marks_.clear();
    notCode_ = 0;
    for (std::size_t i = 0; i < parents.size(); i++) {
        DwarfBlock b;
        b.parent = parents[i];
        blocks_.push_back(b);
    }
}

void Walker::openBlock(int scope) {
    if (lines_ == nullptr || scope <= 0) return;
    if (static_cast<std::size_t>(scope) >= blocks_.size()) return;
    std::size_t before = emittedSize();
    blocks_[scope].begin = label("blk.b", scope);
    blocks_[scope].funclet = funcletNo_;
    defineBlockLabel(blocks_[scope].begin);
    notCode_ += emittedSize() - before;
    Mark m;
    m.size = emittedSize();
    m.notCode = notCode_;
    marks_.push_back(m);
}

void Walker::closeBlock(int scope) {
    if (lines_ == nullptr || scope <= 0) return;
    if (static_cast<std::size_t>(scope) >= blocks_.size()) return;
    if (marks_.empty()) return;
    Mark m = marks_.back();
    marks_.pop_back();
    bool code = (emittedSize() - m.size) > (notCode_ - m.notCode);

    std::size_t before = emittedSize();
    blocks_[scope].end = label("blk.e", scope);
    defineBlockLabel(blocks_[scope].end);
    notCode_ += emittedSize() - before;

    if (!code) {
        blocks_[scope].begin.clear();
        blocks_[scope].end.clear();
    }
}

void Walker::visit(const Block &n) {
    markLine(n);
    openBlock(n.scope());
    // A pad's own code, under a region of its own where the target wants
    // one - the enclosing region split around it, as around a `try` - so a
    // destructor that throws while the exception unwinds terminates.
    const bool scope = n.unwindCleanup() && terminateScopes();
    const int id = scope ? nextLabel() : 0;
    const int msStateAtEntry = msCurState_;
    if (scope) {
        defineLabel(label("cleanup", id));
        openRegion(label("cleanup", id), std::vector<std::string>(), std::vector<int>());
    }
    for (const StmtPtr &s : n.body()) s->accept(*this);
    if (scope) {
        defineLabel(label("cleanupend", id));
        const std::vector<CallSite> pieces = closeRegion(label("cleanupend", id), label("cleanupend", id));
        const std::string pad = terminatePad(id);
        for (std::size_t i = 0; i < pieces.size(); i++) {
            CallSite row = pieces[i];
            row.pad = pad;
            row.terminate = true;
            callSites_.push_back(row);
            exceptionRegion(row.begin, row.end, pad);
        }
    }
    // A cleanup's state outlives its stretch; the block that built the objects
    // destroys them at its '}', and the state in force goes back with them.
    if (msCurState_ != msStateAtEntry) {
        const std::string l = label("stateend", nextLabel());
        defineStateLabel(l);
        msIpRows_.push_back(MsIpRow{ l, msStateAtEntry, funcletNo_ });
        msCurState_ = msStateAtEntry;
    }
    closeBlock(n.scope());
}

void Walker::visit(const If &n) {
    markLine(n);
    int id = nextLabel();
    genTruth(n.cond());
    if (n.elseArm()) {
        branchIfZero(label("else", id));
        n.thenArm().accept(*this);
        jump(label("end", id));
        defineLabel(label("else", id));
        n.elseArm()->accept(*this);
    } else {
        branchIfZero(label("end", id));
        n.thenArm().accept(*this);
    }
    defineLabel(label("end", id));
}

void Walker::visit(const While &n) {
    markLine(n);
    int id = nextLabel();
    jumps_.push_back({ label("end", id), label("begin", id) });
    defineLabel(label("begin", id));
    genTruth(n.cond());
    branchIfZero(label("end", id));
    n.body().accept(*this);
    jump(label("begin", id));
    defineLabel(label("end", id));
    jumps_.pop_back();
}

void Walker::visit(const For &n) {
    markLine(n);

    openBlock(n.scope());
    int id = nextLabel();
    jumps_.push_back({ label("end", id), label("step", id) });

    if (n.init()) n.init()->accept(*this);
    defineLabel(label("begin", id));
    if (n.cond()) {

        markLine(n);
        genTruth(*n.cond());
        branchIfZero(label("end", id));
    }
    n.body().accept(*this);
    defineLabel(label("step", id));
    if (n.step()) {
        markLine(n);
        n.step()->accept(*this);
    }
    jump(label("begin", id));
    defineLabel(label("end", id));
    closeBlock(n.scope());

    jumps_.pop_back();
}

void Walker::visit(const DoWhile &n) {
    markLine(n);
    int id = nextLabel();
    jumps_.push_back({ label("end", id), label("step", id) });

    defineLabel(label("begin", id));
    n.body().accept(*this);
    defineLabel(label("step", id));
    genTruth(n.cond());
    branchIfNotZero(label("begin", id));
    defineLabel(label("end", id));

    jumps_.pop_back();
}

void Walker::visit(const Switch &n) {
    markLine(n);
    int id = nextLabel();

    n.cond().accept(*this);
    for (const Case *c : n.cases())
        caseBranch(c->value(), label("case", c->id()));
    jump(n.defaultCase() ? label("default", n.defaultCase()->id())
                         : label("end", id));

    jumps_.push_back({ label("end", id), "" });
    n.body().accept(*this);
    jumps_.pop_back();
    defineLabel(label("end", id));
}

void Walker::visit(const Case &n) {
    markLine(n);
    defineLabel(label(n.isDefault() ? "default" : "case", n.id()));
    n.body().accept(*this);
}

void Walker::visit(const Goto &n) { markLine(n); jump(userLabel(n.label())); }
void Walker::visit(const FuncletLeave &n) { markLine(n); funcletLeave(userLabel(n.label())); }

void Walker::visit(const Label &n) {
    markLine(n);
    defineLabel(userLabel(n.name()));
    n.body().accept(*this);
}

void Walker::visit(const Conditional &n) {
    int id = nextLabel();
    genTruth(n.cond());
    branchIfZero(label("else", id));
    n.thenArm().accept(*this);
    jump(label("end", id));
    defineLabel(label("else", id));
    n.elseArm().accept(*this);
    defineLabel(label("end", id));
}

void Walker::visit(const Comma &n) {
    n.left().accept(*this);
    n.right().accept(*this);
}

void Walker::visit(const Break &n) { markLine(n); jump(jumps_.back().brk); }

// **The shape is the same on both targets, so it lives here.**
void Walker::openRegion(const std::string &begin,
                        const std::vector<std::string> &types,
                        const std::vector<int> &indices) {
    if (!open_.empty()) {
        OpenRegion &outer = open_.back();
        CallSite piece;
        piece.begin = outer.start;
        piece.end = begin;
        piece.at = outer.at;
        outer.closed.push_back(piece);
    }
    OpenRegion mine;
    mine.start = begin;
    mine.at = ++labelOrder_;
    mine.types = types;
    mine.indices = indices;
    open_.push_back(mine);
}

std::vector<Walker::CallSite> Walker::closeRegion(const std::string &end,
                                                  const std::string &resume, bool handsOver) {
    OpenRegion mine = open_.back();
    open_.pop_back();
    CallSite last;
    last.begin = mine.start;
    last.end = end;
    last.at = mine.at;
    mine.closed.push_back(last);
    const bool any = mine.innerHandsOver || handsOver;
    for (CallSite &piece : mine.closed) piece.handsOver = any;
    if (!open_.empty()) {
        open_.back().start = resume;
        open_.back().at = ++labelOrder_;
        open_.back().innerHandsOver = open_.back().innerHandsOver || any;
    }
    return mine.closed;
}

// The gotos of a pad and the labels it defines - a pad goes on to another of this frame where one of
// its gotos names a label outside it, and resumes the unwind where none does.
static void padJumps(const Stmt &s, std::set<std::string> &to, std::set<std::string> &defined) {
    if (const Block *b = dynamic_cast<const Block *>(&s)) {
        for (const StmtPtr &x : b->body()) padJumps(*x, to, defined);
    } else if (const If *i = dynamic_cast<const If *>(&s)) {
        padJumps(i->thenArm(), to, defined);
        if (i->elseArm() != nullptr) padJumps(*i->elseArm(), to, defined);
    } else if (const Label *l = dynamic_cast<const Label *>(&s)) {
        defined.insert(l->name());
        padJumps(l->body(), to, defined);
    } else if (const Goto *g = dynamic_cast<const Goto *>(&s)) {
        to.insert(g->label());
    }
}

static bool padHandsOver(const Stmt &pad) {
    std::set<std::string> to, defined;
    padJumps(pad, to, defined);
    for (const std::string &l : to)
        if (defined.count(l) == 0) return true;
    return false;
}

// The first Microsoft region's scratch word, wherever in the body it is.
static int firstUnwindHelp(const Stmt &s) {
    if (const Try *t = dynamic_cast<const Try *>(&s)) {
        if (t->unwindHelpSlot() != 0) return t->unwindHelpSlot();
        for (const StmtPtr &x : t->body())
            if (int k = firstUnwindHelp(*x)) return k;
        return 0;
    }
    const Stmt *kids[2] = { nullptr, nullptr };
    if (const Block *b = dynamic_cast<const Block *>(&s)) {
        for (const StmtPtr &x : b->body())
            if (int k = firstUnwindHelp(*x)) return k;
    } else if (const If *i = dynamic_cast<const If *>(&s)) {
        kids[0] = &i->thenArm(); kids[1] = i->elseArm();
    } else if (const While *w = dynamic_cast<const While *>(&s)) {
        kids[0] = &w->body();
    } else if (const For *f = dynamic_cast<const For *>(&s)) {
        kids[0] = f->init(); kids[1] = &f->body();
    } else if (const DoWhile *d = dynamic_cast<const DoWhile *>(&s)) {
        kids[0] = &d->body();
    } else if (const Switch *w = dynamic_cast<const Switch *>(&s)) {
        kids[0] = &w->body();
    } else if (const Case *c = dynamic_cast<const Case *>(&s)) {
        kids[0] = &c->body();
    } else if (const Label *l = dynamic_cast<const Label *>(&s)) {
        kids[0] = &l->body();
    }
    for (const Stmt *k : kids)
        if (k != nullptr)
            if (int slot = firstUnwindHelp(*k)) return slot;
    return 0;
}

// **-2 goes into the scratch word on entry, as cl's prologue writes it**: a call that throws before
// the first region began - a local's constructor - found whatever the stack held there, and the
// runtime took it for a state and destroyed an object that was never built.
void Walker::storeUnwindHelpAtEntry(const Stmt &body, int frame) {
    if (!usesFunclets()) return;
    msUnwindHelpSlot_ = firstUnwindHelp(body);
    if (msUnwindHelpSlot_ == 0) msUnwindHelpSlot_ = frame;
    if (msUnwindHelpSlot_ != 0) storeUnwindHelp(msUnwindHelpSlot_);
}

void Walker::visit(const Try &n) {
    markLine(n);
    if (usesFunclets()) { msTryStatement(n); return; }
    const int id = nextLabel();
    const std::string begin = label("try", id);
    const std::string end = label("tryend", id);
    const std::string pad = label("pad", id);
    const std::string done = label("caught", id);

    defineLabel(begin);
    openRegion(begin, n.types(), n.typeIndices());
    for (std::size_t i = 0; i < n.body().size(); i++) n.body()[i]->accept(*this);
    defineLabel(end);
    const std::vector<CallSite> pieces = closeRegion(end, end, n.hasPad() && padHandsOver(n.pad()));
    jump(done);

    defineLabel(pad);
    landingPad(n.pointerSlot(), n.selectorSlot());
    n.pad().accept(*this);
    defineLabel(done);

    // **Every piece names the same pad and the same types.** They are one
    // region as far as the program is concerned; they are several rows only
    // because something nested inside had to be cut out of the range.
    std::vector<std::string> chain = n.types();
    std::vector<int> chainIx = n.typeIndices();
    // A segment of a try's body carries the try's own types already, so a
    // handler the chain has - the same type under the same index - is not
    // added again: TI's personality reads every row it is given.
    for (std::size_t k = open_.size(); k-- > 0; ) {
        const OpenRegion &outer = open_[k];
        for (std::size_t t = 0; t < outer.types.size(); t++) {
            bool have = false;
            for (std::size_t c = 0; c < chain.size() && c < chainIx.size(); c++)
                if (t < outer.indices.size() && chain[c] == outer.types[t] && chainIx[c] == outer.indices[t]) have = true;
            if (have) continue;
            chain.push_back(outer.types[t]);
            if (t < outer.indices.size()) chainIx.push_back(outer.indices[t]);
        }
    }
    for (std::size_t i = 0; i < pieces.size(); i++) {
        callSite(pieces[i].begin, pieces[i].end, pad, chain, chainIx,
                 n.alsoCleanup(), pieces[i].at, pieces[i].handsOver);
        exceptionRegion(pieces[i].begin, pieces[i].end, pad);
    }
}

// **The Microsoft shape, and what is missing from it is the point.** No pad, no
// selector, no jump over a handler: each handler becomes a function of its own
// and a table says which to call. This frame contributes three labels.
void Walker::msTryStatement(const Try &n) {
    const int id = nextLabel();
    const std::string begin = label("try", id);
    const std::string end = label("tryend", id);
    const std::string resume = label("caught", id);
    // The FuncInfo names one scratch word per frame: the first region's, for all of them. Not inside a
    // handler, whose catch the runtime is in the middle of: it writes -2 there itself when the catch ends.
    if (msUnwindHelpSlot_ == 0) msUnwindHelpSlot_ = n.unwindHelpSlot();
    if (!inFuncletBody()) storeUnwindHelp(msUnwindHelpSlot_);

    // This region's state, inside whatever encloses it; the body's own states
    // follow it in number, which is what tryLow..tryHigh relies on.
    const int parent = msCurState_;
    const int state = static_cast<int>(msStates_.size());
    MsState s;
    s.toState = parent;
    msStates_.push_back(s);
    defineStateLabel(begin);
    msIpRows_.push_back(MsIpRow{ begin, state, funcletNo_ });
    msCurState_ = state;
    for (std::size_t i = 0; i < n.body().size(); i++) n.body()[i]->accept(*this);
    defineStateLabel(end);
    defineLabel(resume);
    exceptionRegion(begin, end, resume);
    for (std::size_t i = 0; i < n.msExits().size(); i++)
        exceptionRegion(begin, end, userLabel(n.msExits()[i]));

    // **A cleanup's objects outlive its range** - the next stretch of the same
    // block chains to it, and the block's end is what reverts the state.
    if (n.cleanup() != nullptr) {
        const std::string funclet = openFunclet(n.cleanup()->pos(), true);
        n.cleanup()->accept(*this);
        endCleanupFunclet();
        leaveFunclet();
        msStates_[state].action = funclet;
        msCurState_ = state;
        return;
    }
    msCurState_ = parent;
    msIpRows_.push_back(MsIpRow{ end, parent, funcletNo_ });

    // The funclets come after their range closes, so none lands between
    // `begin` and `end`; **its row is taken where its body ends**, as cl
    // orders them (an inner `try` before, a handler's `try` after).
    const std::size_t tbAt = msTryBlocks_.size();
    msTryBlocks_.push_back(MsTryBlock());
    MsTryBlock tb;
    tb.tryLow = state;
    tb.tryHigh = static_cast<int>(msStates_.size()) - 1;
    for (std::size_t i = 0; i < n.handlers().size(); i++) {
        const MsHandler &h = n.handlers()[i];
        // A handler's state sits beside the try's, under the same parent: an
        // exception out of the handler has left the try.
        const int hstate = static_cast<int>(msStates_.size());
        MsState hs;
        hs.toState = parent;
        msStates_.push_back(hs);
        MsHandlerRow row;
        row.descriptor = h.descriptor;
        row.objectSlot = h.objectSlot;
        row.byReference = h.byReference;
        row.constPointer = h.constPointer;
        // Entered on the `catch` line, as cl's funclet is.
        row.funclet = openFunclet(h.pos, false);
        const int number = funcletNo_;
        msCurState_ = hstate;
        h.body->accept(*this);
        msCurState_ = parent;
        endFunclet(resume);
        leaveFunclet();
        msFuncletRows_.push_back(MsIpRow{ row.funclet, hstate, number, true });
        tb.handlers.push_back(row);
        if (!h.orNullptr.empty()) {
            row.descriptor = h.orNullptr;
            row.constPointer = false;
            tb.handlers.push_back(row);
        }
    }
    tb.catchHigh = static_cast<int>(msStates_.size()) - 1;
    msTryBlocks_[tbAt] = tb;
}

std::vector<Walker::MsIpRow> Walker::msIpTable() const {
    std::vector<MsIpRow> rows;
    for (const MsIpRow &r : msIpRows_)
        if (r.funclet == 0) rows.push_back(r);
    for (int f : funcletsLifted_) {
        for (const MsIpRow &r : msFuncletRows_)
            if (r.funclet == f) rows.push_back(r);
        for (const MsIpRow &r : msIpRows_)
            if (r.funclet == f) rows.push_back(r);
    }
    return rows;
}

void Walker::visit(const Continue &n) {
    markLine(n);
    for (std::size_t i = jumps_.size(); i-- > 0;) {
        if (!jumps_[i].cont.empty()) {
            jump(jumps_[i].cont);
            return;
        }
    }
}

// **One table, two spellings.** Every byte below was measured against clang for
// both Itanium targets and was written out twice by hand until now; what differs
// is in LsdaSpelling, where the next reader can see the whole of it.
std::string Walker::lsdaTable(const LsdaSpelling &sp, const std::string &symbol,
                              std::vector<std::string> &types) const {
    const std::string L = sp.label;
    const std::string ex = L + "exception." + symbol;
    const std::string ttbase = L + "ttbase." + symbol;
    const std::string ttref = L + "ttbaseref." + symbol;
    const std::string cstBegin = L + "cst.begin." + symbol;
    const std::string cstEnd = L + "cst.end." + symbol;
    const std::string fnBegin = L + "func.begin." + symbol;
    const std::string fnEnd = L + "func.end." + symbol;

    std::string o;
    o += std::string("  ") + sp.section + "\n";
    o += "  .p2align 2\n";
    // A label that is not a temporary, where the linker cuts sections at symbols:
    // with only temporaries the second table in a file was never reached.
    if (sp.atomSymbol) o += "GCC_except_table." + symbol + ":\n";
    o += ex + ":\n";
    o += "  .byte 255\n";                 // LPStart omitted: pads are function-relative
    o += "  .byte 155\n";                 // the type table is indirect, pc-relative
    o += "  .uleb128 " + ttbase + "-" + ttref + "\n";
    o += ttref + ":\n";
    o += "  .byte 1\n";                   // the call-site table is uleb128
    o += "  .uleb128 " + cstEnd + "-" + cstBegin + "\n";
    o += cstBegin + ":\n";

    // **Every call in the function is a row, the ones outside a try
    // included**: a miss makes libc++abi call terminate.
    int action = 1;
    std::string at = fnBegin;
    // A terminate row catches anything, under a type index past the
    // parser's; its pad ends the program.
    int catchAll = 0;
    for (std::size_t i = 0; i < callSites().size(); i++)
        for (std::size_t k = 0; k < callSites()[i].indices.size(); k++)
            if (callSites()[i].indices[k] > catchAll) catchAll = callSites()[i].indices[k];
    catchAll++;
    // **A `throw(T...)` list is a filter**: its types go into the type table past every other index, and
    // the list into the specification table after ttbase, which a negative filter counts into from 1.
    std::vector<std::string> specs;
    std::vector<std::vector<int> > specIndices;
    std::vector<std::string> specTypes;
    int nextIndex = catchAll;
    for (const CallSite &c : callSites())
        for (const std::string &t : c.types) {
            if (t.compare(0, 5, "$spec") != 0) continue;
            if (std::find(specs.begin(), specs.end(), t) != specs.end()) continue;
            specs.push_back(t);
            specIndices.push_back(std::vector<int>());
            for (std::size_t from = 6; from <= t.size(); ) {
                std::size_t to = t.find(',', from);
                if (to == std::string::npos) to = t.size();
                if (to > from) {
                    specIndices.back().push_back(++nextIndex);
                    specTypes.push_back(t.substr(from, to - from));
                }
                from = to + 1;
            }
        }
    auto filterOf = [&](const std::string &t) {
        int offset = 0;
        for (std::size_t i = 0; i < specs.size() && specs[i] != t; i++)
            offset += static_cast<int>(specIndices[i].size()) + 1;
        return -(offset + 1);
    };
    // **Sorted by address, which is safe now and was not before.**
    std::vector<CallSite> rows = callSites();
    for (std::size_t i = 1; i < rows.size(); i++)
        for (std::size_t j = i; j > 0 && rows[j - 1].at > rows[j].at; j--) {
            CallSite t = rows[j - 1]; rows[j - 1] = rows[j]; rows[j] = t;
        }
    for (std::size_t i = 0; i < rows.size(); i++) {
        const CallSite &c = rows[i];
        o += "  .uleb128 " + at + "-" + fnBegin + "\n";
        o += "  .uleb128 " + c.begin + "-" + at + "\n";
        o += "  .byte 0\n";
        o += "  .byte 0\n";
        o += "  .uleb128 " + c.begin + "-" + fnBegin + "\n";
        o += "  .uleb128 " + c.end + "-" + c.begin + "\n";
        o += "  .uleb128 " + c.pad + "-" + fnBegin + "\n";
        // No handler at all is a *cleanup*.
        o += "  .uleb128 " + std::to_string(c.types.empty() && !c.terminate ? 0 : action) + "\n";
        // **A `try` inside a cleanup region carries one record more.**
        action += 2 * static_cast<int>(c.types.size() + (c.cleanup ? 1 : 0) + (c.terminate ? 1 : 0));
        at = c.end;
    }
    o += "  .uleb128 " + at + "-" + fnBegin + "\n";
    o += "  .uleb128 " + fnEnd + "-" + at + "\n";
    o += "  .byte 0\n";
    o += "  .byte 0\n";
    o += cstEnd + ":\n";

    // The action table: a type index and the offset to the next record, 0
    // saying there is no next, so the chain ends and the exception goes on
    // unwinding. **The index is the parser's, not this table's.**
    types.clear();
    if (!specTypes.empty()) types.resize(specTypes.size() + static_cast<std::size_t>(catchAll));
    for (std::size_t i = 0; i < specTypes.size(); i++)
        types[static_cast<std::size_t>(catchAll) + i] = specTypes[i];
    for (std::size_t i = 0; i < rows.size(); i++) {
        const CallSite &c = rows[i];
        if (c.terminate) {
            if (types.size() < static_cast<std::size_t>(catchAll)) types.resize(static_cast<std::size_t>(catchAll));
            types[static_cast<std::size_t>(catchAll) - 1] = std::string();
            o += "  .sleb128 " + std::to_string(catchAll) + "\n";
            o += "  .byte 0\n";
            continue;
        }
        // The filter is a signed LEB128: one byte to 63, two past it (a try with 256 handlers), and the
        // specification's negative index likewise; the displacement to the next record stays one byte.
        for (std::size_t k = 0; k < c.types.size(); k++) {
            if (c.types[k].compare(0, 5, "$spec") == 0) {
                o += "  .sleb128 " + std::to_string(filterOf(c.types[k])) + "\n";
                const bool more = k + 1 < c.types.size() || c.cleanup;
                o += "  .byte " + std::string(more ? "1" : "0") + "\n";
                continue;
            }
            const std::size_t at = k < c.indices.size()
                                       ? static_cast<std::size_t>(c.indices[k])
                                       : types.size() + 1;
            if (types.size() < at) types.resize(at);
            types[at - 1] = c.types[k];
            o += "  .sleb128 " + std::to_string(at) + "\n";
            const bool more = k + 1 < c.types.size() || c.cleanup;
            o += "  .byte " + std::string(more ? "1" : "0") + "\n";
        }
        // Filter 0 is "cleanup": not a handler, but a reason to stop here in
        // phase 2 and run the pad. It ends the chain.
        if (c.cleanup && !c.types.empty()) {
            o += "  .byte 0\n";
            o += "  .byte 0\n";
        }
    }
    o += "  .p2align 2\n";
    // **Written backwards**: index 1 is the entry just before Lttbase.
    for (std::size_t i = types.size(); i-- > 0; ) {
        const std::string here = L + "ti." + symbol + "." + std::to_string(i);
        o += here + ":\n";
        if (types[i].empty()) o += "  .long 0\n";          // catch (...)
        else o += "  .long " + std::string(sp.typePrefix) + types[i] +
                  sp.typeSuffix + "-" + here + "\n";
    }
    o += ttbase + ":\n";
    for (std::size_t i = 0; i < specs.size(); i++) {
        for (int ix : specIndices[i]) o += "  .uleb128 " + std::to_string(ix) + "\n";
        o += "  .byte 0\n";
    }
    o += "  .p2align 2\n";
    return o;
}
