#pragma once

#include "Backend.h"
#include "Spelling.h"
#include "X86_64Linux.h"

#include <iosfwd>
#include <set>
#include <string>
#include <vector>

class MasmSpelling final : public Spelling {
public:
    // `comdat` says the assembler reads `SEGMENT ... COMDAT`: the project's does, ml64 does not - see Syntax in Backend.h.
    MasmSpelling(std::string &o, bool comdat) : comdat_(comdat), o_(o) {}

    void ins(const std::string &m) override;
    void ins(const std::string &m, const Op &a) override;
    void ins(const std::string &m, const Op &a, const Op &b) override;

    void defLabel(const std::string &l) override;
    void functionBegin(const std::string &name, bool exported,
                       bool mergeable = false) override;
    void weakDefinition(const std::string &name) override;
    // The clause a mergeable function's unwind data, funclets and their unwind data take, so each goes with the copy of the function it belongs to.
    std::string associative() { return mergeable_ ? " ASSOCIATIVE(" + mangledName() + ")" : std::string(); }
    // Whether records the linker should fold are written as COMDATs.
    bool comdat() const { return comdat_; }
    void prologue(int frameSize, const std::string &lsda, int outgoing, int realign) override;
    void stateLabel(const std::string &l) override;
    // The registers an optimizer keeps locals in, saved after the frame pointer is set - see prologue.
    void calleeSaves(const std::vector<SavedReg> &saves) override { saves_ = saves; }
    // **Whether a FuncInfo follows is the code generator's answer, not this one's.**
    void noteHasEh(bool yes) override { hasEh_ = yes; }
    // The unwind codes the prologue described, written out by functionEnd -
    // which is where the labels they measure against exist.
    std::string unwindData_;
    int unwindCodes_ = 0;
    std::string fnName_;
    // The frame this function allocated.
    int frameSize_ = 0;
    // Whether this function has a handler: it decides the flags in the unwind header and whether __CxxFrameHandler3 and a FuncInfo follow the codes.
    bool hasEh_ = false;
    // Whether the last instruction spelled was a call - see stateLabel.
    bool afterCall_ = false;
    void raw(const std::string &text);
    // Written by postamble, after every chunk the code generator built. The
    // funclets' .pdata goes here: see funcletPdata_ in MasmCodeGen.
    std::string trailer_;
    std::string mangledName() { return mangle(fnName_); }
    // A label written by the Walker, spelled the way this file spells one. Raw
    // emission goes through the same door as defLabel, or a table names
    // `.L.main.caught.0` where the code defines `$_L_main_caught_0`.
    std::string labelName(const std::string &l) { return mangle(l); }
    // `$LNend$` and the rest are the prefix before the function's MASM name - see prologue and
    // functionEnd - so -g's records name them that way, not as one name spelled whole (1.30).
    std::string codeLabelText(const std::string &prefix, const std::string &symbol) override {
        return prefix + mangle(symbol);
    }
    void functionEnd(const std::string &name) override;

    // -g: the source files and lines, in the directives GRIDE's masm reads - `.FILE n, "path"` and
    // `.LINE line, n` - as c90 writes them; ml64 has no such words (1.29, 2026-10-22).
    void fileEntry(int n, const std::string &name) override;
    void location(int file, int line, int column) override;
    // A funclet PROC's lines are a run of their own; masm makes its table per PROC (1.29).
    void funcletLinesBegin() override;
    void funcletLinesEnd() override;
    std::string locationDirective(int fn, int file, int line, int column) const override;
    // -g: the CodeView writeCodeView makes for clang, spelled for GRIDE's masm (see Masm.cpp).
    void codeView(const std::string &gnu);

    void globl(const std::string &name) override;
    void textSection() override;
    void rodataSection() override;
    void dataSection() override;
    void bssSection() override;
    void tlsDataSection() override;
    void tlsBssSection() override { tlsDataSection(); }
    void tlsInitialiserEntry(const std::string &fn) override;
    bool tlsOperandsSpelled() const override { return !comdat_; }
    void tlsOffsetWord(const std::string &symbol) override;
    void objectType(const std::string &name) override;
    void objectSize(const std::string &name, int size) override;
    void align(int n) override;
    void loopAlign(int bytes) override;
    void zero(int n) override;
    void dataInt(int size, long long v) override;
    void dataSym(const std::string &sym, long long off) override;
    void dataBytes(const std::string &bytes) override;

    void flagExchange() override;
    void atomicHelper(const std::string &name) override;
    void predefine(const std::vector<std::string> &names) override;
    void initialiserEntry(const std::string &fn, bool dsoHandle) override;
    // A `div` or `idiv` given up as a keyword is still an instruction here: written as its bytes (1.30).
    void finishText(std::vector<std::string> &chunks) override;
    void preamble(std::ostream &sink) override;
    void postamble(std::ostream &sink) override;

private:
    const bool comdat_;
    std::string &o_;
    std::vector<SavedReg> saves_;
    enum Seg { None, Code, Data, Const, Bss, Tls } seg_ = None;

    // **A mergeable definition is a COMDAT of its own.** A function opens one at functionBegin;
    // a global's is opened at its ALIGN, where its alignment is known, and closed by the next
    // object's ALIGN, a section change, a function or the end of the file.
    bool mergeable_ = false;          // the open function is in a COMDAT
    std::string pendingComdat_;       // weakDefinition named the next object
    std::string dataBlock_;           // the segment an ENDS is owed to
    bool dataBlockUsed_ = false;      // its object has begun: the next ALIGN closes it
    void openDataBlock(int align);
    void closeDataBlock();

    std::string pending_;

    // -g: the line entries of the function being written - the last `.LINE` and where it went (one
    // no instruction follows is replaced by the next), and every (file, line) given a run already.
    bool inFunction_ = false;
    std::string lastLoc_;
    std::size_t lastLocAt_ = 0;
    int lastLocFile_ = 0, lastLocLine_ = 0;
    std::set<std::pair<int, int> > linesRun_;
    // The function's line state, set aside while a funclet's lines are written.
    struct LineState { std::string lastLoc; std::size_t lastLocAt; int lastLocFile, lastLocLine; std::set<std::pair<int, int> > linesRun; };
    std::vector<LineState> outerLines_;
    std::string mangleNames(const std::string &expr);

    std::set<std::string> defined_, exported_, referenced_, unreserved_;
    std::set<std::string> tlsWords_;
    // Keywords this file writes and defines a symbol by, renamed and warned about once (1.30).
    std::set<std::string> renamed_;
    // Referenced inside a SECTIONREL expression, which ml64 refuses to add to a `:PROC` (A2102).
    std::set<std::string> referencedData_;

    struct Rendered {
        std::string text;
        bool isMem = false, isImm = false, isXmm = false;
    };
    Rendered render(const Op &x);
    std::string mangle(const std::string &name);
    void flushPending();
    void items(const char *dir, const std::vector<std::string> &it);
};

class MasmCodeGen final : public X86_64Linux {
public:
    MasmCodeGen(std::ostream &sink, const Target &target, const Abi &abi, bool comdat)
        : X86_64Linux(sink, target, abi), masm_(out_, comdat) { a_ = plain_ = &masm_; }

    void run(const Program &program) override;

private:
    bool usesFunclets() const override { return true; }
    bool localsAboveFrameBase() const override { return true; }
    std::string beginFunclet(std::size_t pos, int number, bool cleanup) override;
    void funcletLeave(const std::string &label) override;
    void endFunclet(const std::string &resume) override;
    void endCleanupFunclet() override;
    void closeFunclet(const std::string &tail);
    // **A Windows local is `frameSize**`.
    int establisherOffset(int slot) const;

    void storeUnwindHelp(int slot) override;
    void emitExceptionTables(const Function &fn) override;

    // A funclet is the text the body appended, lifted back out in order.
    std::string funclets_;
    // The funclets' .pdata goes to MasmSpelling::trailer_, after every function:
    // .pdata has to be sorted by the address it describes and every funclet
    // lives in .text$x, so emitting beside the parent gives LNK1223.
    std::vector<std::size_t> funcletMarks_;
    int funcletIndex_ = 0;
    std::string funcletSymbol_;
    // The funclets the one being walked was opened inside, with their entries' -g locations.
    std::vector<std::pair<std::string, std::string> > outerFunclets_;
    const char *funcletKind_ = "$catch$";
    // -g: the variables and types as CodeView, which GRIDE's masm carries into the object
    // (writeDebug); not for ml64, which the driver refuses -g for (1.29).
    bool writesDwarf() const override { return masm_.comdat(); }
    void writeDebug(const std::vector<DwarfFunction> &fns,
                    const std::vector<DwarfGlobal> &globals) override;
    bool emitsOwnRtti() const override { return true; }
    // The four objects the Microsoft ABI wants per thrown type.
    void emitThrowInfo(const Program &program);
    // The five objects the Microsoft ABI wants per class with a vftable, for the same reason and in the same place.
    void emitClassRtti(const Program &program);
    std::string record(const char *segment, int align, const std::string &name, bool first,
                       bool writable = false);

    MasmSpelling masm_;
};
