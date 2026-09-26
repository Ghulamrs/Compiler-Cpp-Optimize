#include "Walker.h"

#include <algorithm>

#include "../Source.h"

void Walker::markLine(const Stmt &n) { markLine(n.pos()); }

void Walker::markLine(std::size_t pos) {
    if (lines_ == nullptr) return;

    if (pos == 0) return;
    Source::Place at = lines_->locate(pos);
    std::size_t before = emittedSize();
    emitLoc(at.file + 1, at.line, at.column);

    notCode_ += emittedSize() - before;
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
    defineLabel(blocks_[scope].begin);
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
    defineLabel(blocks_[scope].end);
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
        msIpRows_.push_back(MsIpRow{ l, msStateAtEntry });
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
                                                  const std::string &resume) {
    OpenRegion mine = open_.back();
    open_.pop_back();
    CallSite last;
    last.begin = mine.start;
    last.end = end;
    last.at = mine.at;
    mine.closed.push_back(last);
    if (!open_.empty()) {
        open_.back().start = resume;
        open_.back().at = ++labelOrder_;
    }
    return mine.closed;
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
    const std::vector<CallSite> pieces = closeRegion(end, end);
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
                 n.alsoCleanup(), pieces[i].at);
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
    // The FuncInfo names one scratch word per frame: the first region's, for all of them.
    if (msUnwindHelpSlot_ == 0) msUnwindHelpSlot_ = n.unwindHelpSlot();
    storeUnwindHelp(msUnwindHelpSlot_);

    // This region's state, inside whatever encloses it; the body's own states
    // follow it in number, which is what tryLow..tryHigh relies on.
    const int parent = msCurState_;
    const int state = static_cast<int>(msStates_.size());
    MsState s;
    s.toState = parent;
    msStates_.push_back(s);
    defineStateLabel(begin);
    msIpRows_.push_back(MsIpRow{ begin, state });
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
        const std::string funclet = beginFunclet();
        n.cleanup()->accept(*this);
        endCleanupFunclet();
        msStates_[state].action = funclet;
        msCurState_ = state;
        return;
    }
    msCurState_ = parent;
    msIpRows_.push_back(MsIpRow{ end, parent });

    // The funclets come after the range they belong to is closed, so nothing
    // they emit lands between `begin` and `end` - those bound the addresses the
    // runtime matches a thrown object against, and a handler must not be inside.
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
        row.funclet = beginFunclet();
        msCurState_ = hstate;
        h.body->accept(*this);
        msCurState_ = parent;
        endFunclet(resume);
        msFuncletRows_.push_back(MsIpRow{ row.funclet, hstate });
        tb.handlers.push_back(row);
    }
    tb.catchHigh = static_cast<int>(msStates_.size()) - 1;
    msTryBlocks_.push_back(tb);
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
    for (std::size_t i = 0; i < rows.size(); i++) {
        const CallSite &c = rows[i];
        if (c.terminate) {
            if (types.size() < static_cast<std::size_t>(catchAll)) types.resize(static_cast<std::size_t>(catchAll));
            types[static_cast<std::size_t>(catchAll) - 1] = std::string();
            o += "  .byte " + std::to_string(catchAll) + "\n";
            o += "  .byte 0\n";
            continue;
        }
        for (std::size_t k = 0; k < c.types.size(); k++) {
            const std::size_t at = k < c.indices.size()
                                       ? static_cast<std::size_t>(c.indices[k])
                                       : types.size() + 1;
            if (types.size() < at) types.resize(at);
            types[at - 1] = c.types[k];
            o += "  .byte " + std::to_string(at) + "\n";
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
    o += "  .p2align 2\n";
    return o;
}
