#pragma once

#include "Backend.h"
#include "Dwarf.h"

#include <cstddef>
#include <string>
#include <vector>

class Source;

class Walker : public CodeGen {
public:
    // **What differs between the two Itanium tables, and it is only spelling.**
    // The bytes, the call-site rows, the action records and the reverse type
    // table were written out twice by hand; these five fields are the difference.
    struct LsdaSpelling {
        const char *label;        // ".L" on ELF, "L" on Mach-O
        const char *section;      // the directive this table is written into
        bool atomSymbol;          // Mach-O cuts sections at symbols, so it needs one
        const char *typePrefix;   // a type_info is reached through a local stub on
        const char *typeSuffix;   // ELF and through the GOT on Mach-O
    };

    void visit(const ExprStmt &n) override;
    void visit(const Block &n) override;
    void visit(const If &n) override;
    void visit(const While &n) override;
    void visit(const For &n) override;
    void visit(const DoWhile &n) override;
    void visit(const Switch &n) override;
    void visit(const Case &n) override;
    void visit(const Goto &n) override;
    void visit(const Label &n) override;
    void visit(const Conditional &n) override;
    void visit(const Comma &n) override;
    void visit(const Break &n) override;
    void visit(const Continue &n) override;
    void visit(const Try &n) override;

    void setLineSource(const Source *s, const std::string &dir) override {
        lines_ = s;
        compDir_ = dir;
    }

    const std::vector<DwarfBlock> &blocks() const { return blocks_; }

protected:

    void msTryStatement(const Try &n);

    void markLine(const Stmt &n);

    void markLine(std::size_t pos);
    const Source *lineSource() const { return lines_; }
    const std::string &compDir() const { return compDir_; }
    virtual void emitLoc(int file, int line, int column) { (void)file; (void)line; (void)column; }

    virtual void defineLabel(const std::string &l) = 0;
    // Where a Microsoft frame's exception state changes; see Spelling::stateLabel.
    virtual void defineStateLabel(const std::string &l) { defineLabel(l); }
    virtual void jump(const std::string &l) = 0;
    virtual void branchIfZero(const std::string &l) = 0;
    virtual void branchIfNotZero(const std::string &l) = 0;

    virtual void caseBranch(long long v, const std::string &l) = 0;

    virtual void genTruth(const Expr &e) = 0;
    virtual std::string label(const char *kind, int id) const = 0;
    virtual std::string userLabel(const std::string &name) const = 0;

    virtual std::size_t emittedSize() = 0;

    // **What a backend has to be told about a landing pad, and no more.**
    virtual void landingPad(int pointerSlot, int selectorSlot) = 0;
    // A call between the two labels that throws continues at the target: the
    // pad, or where the function resumes after a handler ran as a funclet.
    virtual void exceptionRegion(const std::string &begin, const std::string &end, const std::string &target) {
        (void)begin; (void)end; (void)target;
    }

    // One row of the call-site table: a call between `begin` and `end` that
    // throws goes to `pad`, catching these types in this order. Collected and
    // not emitted, since the table follows the body and the rows are shared.
    struct CallSite {
        std::string begin;
        std::string end;
        std::string pad;
        std::vector<std::string> types;
        // Parallel to `types`: the selector index the parser gave each one.
        std::vector<int> indices;
        bool cleanup = false;   // a trailing action record with filter 0
        bool terminate = false; // a throw out of here ends the program: a pad's own code
        // Where this segment begins, counted in the order labels are defined -
        // which is address order, since the walk emits as it goes. Only usable
        // as a sort key because the segments below are disjoint.
        int at = 0;
    };

    // **A region open right now, and the segments of it already closed.**
    struct OpenRegion {
        std::string start;
        int at = 0;
        std::vector<CallSite> closed;
        // **What this region catches**, carried so a region nested inside it
        // can put these on the end of its own action chain.
        std::vector<std::string> types;
        std::vector<int> indices;
    };
    // **Rows are used in the order they are registered and never sorted.**
    void callSite(const std::string &begin, const std::string &end,
                  const std::string &pad,
                  const std::vector<std::string> &types,
                  const std::vector<int> &indices,
                  bool cleanup = false, int at = 0) {
        CallSite s;
        s.begin = begin;
        s.end = end;
        s.pad = pad;
        s.types = types;
        s.indices = indices;
        s.cleanup = cleanup;
        s.at = at;
        callSites_.push_back(s);
    }

    // Open a region here, splitting whatever encloses it.
    void openRegion(const std::string &begin,
                    const std::vector<std::string> &types,
                    const std::vector<int> &indices);
    // Close it, hand back its segments, and reopen the enclosing one past
    // `resume` - which is the inner's end, so the enclosing still covers the
    // inner's landing pad and handler.
    std::vector<CallSite> closeRegion(const std::string &end,
                                      const std::string &resume);
    const std::vector<CallSite> &callSites() const { return callSites_; }
    void clearCallSites() { callSites_.clear(); open_.clear(); labelOrder_ = 0; }

    // The whole table, as text, for a caller that knows where to put it. ELF's
    // stubs and its personality comdat are not here: they are that target's, and
    // they follow the table rather than living in it.
    std::string lsdaTable(const LsdaSpelling &sp, const std::string &symbol,
                          std::vector<std::string> &types) const;

    // **Does this target call handlers, or jump to them?**
    virtual bool usesFunclets() const { return false; }

    // **Does the table say a throw out of a cleanup pad terminates?** TI's does, with a scope over the pad.
    virtual bool terminateScopes() const { return false; }
    // The pad that scope lands on, where the table needs one: empty where its word does the ending, as TI's.
    virtual std::string terminatePad(int id) { (void)id; return std::string(); }

    // Write -2 into the runtime's scratch word.
    virtual void storeUnwindHelp(int slot) { (void)slot; }

    // A cleanup funclet is opened the same way and closed differently.
    virtual void endCleanupFunclet() {}

    // Open a handler funclet and answer its symbol; close it naming the address
    // in the parent to continue at, which a funclet returns in rax. Between the
    // two the body is walked as if inline, the funclet setting rbp from the parent.
    virtual std::string beginFunclet() { return std::string(); }
    virtual void endFunclet(const std::string &resume) { (void)resume; }

    // One `try` as the Microsoft tables describe it: the range guarded, where
    // to continue after a handler, the frame slot the runtime scribbles in,
    // and one row per handler.
    struct MsHandlerRow {
        std::string descriptor;   // empty for catch (...)
        int objectSlot = 0;
        bool byReference = false;
        bool constPointer = false;
        std::string funclet;
    };
    struct MsTryRegion {
        std::string begin;
        std::string end;
        std::string resume;
        int unwindHelpSlot = 0;
        std::vector<MsHandlerRow> handlers;
        // A cleanup region instead of a try: no handler, one funclet that
        // runs destructors while the exception carries on past this frame.
        bool isCleanup = false;
        std::string cleanupFunclet;
    };
    void msTry(const MsTryRegion &r) { msTries_.push_back(r); }
    const std::vector<MsTryRegion> &msTries() const { return msTries_; }
    void clearMsTries() { msTries_.clear(); }

    void resetBlocks(const std::vector<int> &parents);

    void openBlock(int scope);
    void closeBlock(int scope);

    int nextLabel() { return labels_++; }
    void resetLabels() { labels_ = 0; }
    struct JumpTargets { std::string brk; std::string cont; };
    std::vector<JumpTargets> jumps_;

private:
    int labels_ = 0;
    std::vector<CallSite> callSites_;
    std::vector<OpenRegion> open_;
    int labelOrder_ = 0;
    std::vector<MsTryRegion> msTries_;
    const Source *lines_ = nullptr;
    std::string compDir_;
    std::vector<DwarfBlock> blocks_;

    std::size_t notCode_ = 0;
    struct Mark { std::size_t size; std::size_t notCode; };
    std::vector<Mark> marks_;
};
