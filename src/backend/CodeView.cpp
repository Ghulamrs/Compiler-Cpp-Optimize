#include "CodeView.h"
#include "CodeViewTypes.h"

#include <cctype>

namespace {

// Symbol record kinds (cvinfo.h) and the subsection that carries them.
const int kSObjName = 0x1101, kSCompile3 = 0x113c, kSFrameProc = 0x1012;
const int kSGProc32 = 0x1110, kSLProc32 = 0x110f, kSEnd = 0x0006;
const int kDebugSSymbols = 0xf1, kCvSignatureC13 = 4, kMachineAmd64 = 0xd0;
const int kSRegRel32 = 0x1111, kSBlock32 = 0x1103, kSUdt = 0x1108;
const int kSGData32 = 0x110d, kSLData32 = 0x110c, kCvAmd64Rbp = 334, kCvAmd64Rbx = 331;

// S_FRAMEPROC flags: locals and parameters both off RBP (encoded 2, bits 14-15 and 16-17).
const int kFrameFlags = (2 << 14) | (2 << 16);

void line(std::string &o, const std::string &text) { o += text; o += '\n'; }

void num(std::string &o, const char *dir, long long v) {
    o += dir;
    o += ' ';
    o += std::to_string(v);
    o += '\n';
}

// **One record, measured by its own labels.** A symbol record is its length
// (not counting the length field), its kind, its fields, and padding to four.
class Records {
public:
    Records(std::string &o, const char *prefix) : o_(o), prefix_(prefix) {}

    void begin(int kind) {
        const std::string n = std::to_string(++count_);
        end_ = prefix_ + "r" + n + ".e";
        const std::string b = prefix_ + "r" + n + ".b";
        line(o_, "  .short " + end_ + "-" + b);
        line(o_, b + ":");
        num(o_, "  .short", kind);
    }
    void end() {
        line(o_, "  .p2align 2");
        line(o_, end_ + ":");
    }

    // A DEBUG_S_SYMBOLS subsection around the records written between the two.
    void openSubsection() {
        const std::string n = std::to_string(++count_);
        subEnd_ = prefix_ + "s" + n + ".e";
        const std::string b = prefix_ + "s" + n + ".b";
        num(o_, "  .long", kDebugSSymbols);
        line(o_, "  .long " + subEnd_ + "-" + b);
        line(o_, b + ":");
    }
    void closeSubsection() {
        line(o_, subEnd_ + ":");
        line(o_, "  .p2align 2");
    }

    std::string &out() { return o_; }

private:
    std::string &o_;
    std::string prefix_;
    std::string end_;
    std::string subEnd_;
    int count_ = 0;
};

void name(std::string &o, const std::string &v) {
    o += "  .asciz \"";
    for (char c : v) {
        if (c == '"' || c == '\\') o += '\\';
        o += c;
    }
    o += "\"\n";
}

void writeCompileUnit(Records &r, CodeViewLanguage language, const std::string &objectName) {
    std::string &o = r.out();
    r.openSubsection();
    r.begin(kSObjName);
    num(o, "  .long", 0);
    name(o, objectName);
    r.end();

    // The front and back end versions are cxx1's own; the language is what
    // decides how cdb prints a name and evaluates an expression.
    r.begin(kSCompile3);
    num(o, "  .long", static_cast<int>(language));
    num(o, "  .short", kMachineAmd64);
    for (int i = 0; i < 8; i++) num(o, "  .short", i % 4 == 0 ? 1 : 0);
    name(o, "cxx1");
    r.end();
    r.closeSubsection();
}

// One object in a scope: a frame slot off RBP, or a static local's own symbol.
// `frameReg` is the register the slot is measured from: rbp, or rbx for a realigned frame (DWARF's 3).
void writeObject(Records &r, const Local &l, int frameBias, int frameReg, CodeViewTypes &types, const Spell &spell) {
    std::string &o = r.out();
    if (!l.staticName.empty()) {
        r.begin(kSLData32);
        num(o, "  .long", types.index(l.type));
        line(o, "  .secrel32 " + spell(l.staticName));
        line(o, "  .secidx " + spell(l.staticName));
        name(o, l.name);
        r.end();
        return;
    }
    r.begin(kSRegRel32);
    num(o, "  .long", frameBias - l.offset);
    num(o, "  .long", types.index(l.type));
    num(o, "  .short", frameReg);
    name(o, l.name);
    r.end();
}

// **A block belongs to the procedure whose code it is in**: the function's own, or the funclet it was
// walked inside (DwarfBlock::funclet) - the others' ranges are in another section (1.29).
bool inProcedure(const DwarfFunction &f, std::size_t b) { return f.blocks[b].funclet == f.funclet; }

bool declaresAnything(const DwarfFunction &f, int scope) {
    if (f.locals != nullptr)
        for (const Local &l : *f.locals)
            if (l.scope == scope) return true;
    for (std::size_t b = 1; b < f.blocks.size(); b++)
        if (f.blocks[b].parent == scope && inProcedure(f, b) && declaresAnything(f, static_cast<int>(b))) return true;
    return false;
}

void writeObjects(Records &r, const DwarfFunction &f, int scope, CodeViewTypes &types, const Spell &spell) {
    if (f.locals == nullptr) return;
    // **Parameters first and in order**: cdb counts off the procedure's arguments from the top.
    for (const Local &l : *f.locals)
        if (l.scope == scope && l.isParam) writeObject(r, l, f.frameBias, f.frameBaseReg == 3 ? kCvAmd64Rbx : kCvAmd64Rbp, types, spell);
    for (const Local &l : *f.locals)
        if (l.scope == scope && !l.isParam) writeObject(r, l, f.frameBias, f.frameBaseReg == 3 ? kCvAmd64Rbx : kCvAmd64Rbp, types, spell);
}

void writeScope(Records &r, const DwarfFunction &f, int scope, CodeViewTypes &types, const Spell &spell);

// One block with its objects and the blocks in it: an S_BLOCK32 over its range, or - a block that
// left no code, so no range - its objects in the scope around it.
void writeBlock(Records &r, const DwarfFunction &f, std::size_t b, CodeViewTypes &types, const Spell &spell) {
    const DwarfBlock &block = f.blocks[b];
    const int id = static_cast<int>(b);
    if (block.begin.empty() || block.end.empty()) {
        writeScope(r, f, id, types, spell);
        return;
    }
    std::string &o = r.out();
    r.begin(kSBlock32);
    line(o, "  .long 0, 0");
    line(o, "  .long " + spell(block.end) + "-" + spell(block.begin));
    line(o, "  .secrel32 " + spell(block.begin));
    line(o, "  .secidx " + spell(block.begin));
    name(o, "");
    r.end();
    writeScope(r, f, id, types, spell);
    r.begin(kSEnd);
    r.end();
}

void writeScope(Records &r, const DwarfFunction &f, int scope, CodeViewTypes &types, const Spell &spell) {
    writeObjects(r, f, scope, types, spell);
    for (std::size_t b = 1; b < f.blocks.size(); b++)
        if (f.blocks[b].parent == scope && inProcedure(f, b) && declaresAnything(f, static_cast<int>(b)))
            writeBlock(r, f, b, types, spell);
}

// **A funclet's scope is its parent's frame**, as cl writes it: the parent's
// parameters and locals (rbp is the parent's), the enclosing blocks' objects
// without ranges, then the handler's own blocks, the caught object's too.
void writeFuncletScope(Records &r, const DwarfFunction &f, CodeViewTypes &types, const Spell &spell) {
    writeObjects(r, f, 0, types, spell);
    std::vector<bool> around(f.blocks.size(), false);
    for (std::size_t b = 1; b < f.blocks.size(); b++)
        if (inProcedure(f, b))
            for (int p = f.blocks[b].parent; p > 0; p = f.blocks[static_cast<std::size_t>(p)].parent)
                if (!inProcedure(f, static_cast<std::size_t>(p))) around[static_cast<std::size_t>(p)] = true;
    for (std::size_t p = 1; p < f.blocks.size(); p++)
        if (around[p]) writeObjects(r, f, static_cast<int>(p), types, spell);
    for (std::size_t b = 1; b < f.blocks.size(); b++) {
        const int parent = f.blocks[b].parent;
        if (inProcedure(f, b) && (parent <= 0 || !inProcedure(f, static_cast<std::size_t>(parent))) &&
            declaresAnything(f, static_cast<int>(b)))
            writeBlock(r, f, b, types, spell);
    }
}

unsigned procedureType(const DwarfFunction &f, CodeViewTypes &types) {
    std::vector<const Type *> params;
    if (f.locals != nullptr)
        for (const Local &l : *f.locals)
            if (l.isParam && l.scope == 0) params.push_back(l.type);
    return types.procedure(f.returns, params, false);
}

void writeFunction(Records &r, const DwarfFunction &f, int id, CodeViewTypes &types, const Spell &spell) {
    std::string &o = r.out();
    r.openSubsection();
    r.begin(f.external ? kSGProc32 : kSLProc32);
    line(o, "  .long 0, 0, 0");
    line(o, "  .long " + f.codeEnd + "-" + f.symbol);
    line(o, "  .long " + f.prologEnd + "-" + f.symbol);
    line(o, "  .long " + f.codeEnd + "-" + f.symbol);
    // A funclet has no type of its own, as cl writes it (T_NOTYPE).
    num(o, "  .long", f.funclet != 0 ? 0u : procedureType(f, types));
    line(o, "  .secrel32 " + f.symbol);
    line(o, "  .secidx " + f.symbol);
    num(o, "  .byte", 0);
    name(o, f.name);
    r.end();

    r.begin(kSFrameProc);
    num(o, "  .long", f.frameBias);
    line(o, "  .long 0, 0, 0, 0");
    num(o, "  .short", 0);
    num(o, "  .long", kFrameFlags);
    r.end();

    if (f.funclet != 0) writeFuncletScope(r, f, types, spell);
    else writeScope(r, f, 0, types, spell);

    r.begin(kSEnd);
    r.end();
    r.closeSubsection();
    line(o, "  .cv_linetable " + std::to_string(id) + ", " + f.symbol + ", " + f.codeEnd);
}

// The file's globals and statics, and a S_UDT for every class, union and enum named.
void writeGlobals(Records &r, const std::vector<DwarfGlobal> &globals, CodeViewTypes &types,
                  const Spell &spell) {
    // **No empty subsection**: link.exe then lost the file table after it, and no line had a file.
    if (globals.empty() && types.named().empty()) return;
    std::string &o = r.out();
    r.openSubsection();
    for (const DwarfGlobal &g : globals) {
        r.begin(g.external ? kSGData32 : kSLData32);
        num(o, "  .long", types.index(g.type));
        line(o, "  .secrel32 " + spell(g.symbol));
        line(o, "  .secidx " + spell(g.symbol));
        name(o, g.name);
        r.end();
    }
    for (const std::pair<std::string, unsigned> &udt : types.named()) {
        r.begin(kSUdt);
        num(o, "  .long", udt.second);
        name(o, udt.first);
        r.end();
    }
    r.closeSubsection();
}

void openDebugS(std::string &o, const std::string &associative) {
    line(o, "  .section .debug$S,\"dr\"" +
                (associative.empty() ? std::string() : ",associative," + associative));
    line(o, "  .p2align 2");
    num(o, "  .long", kCvSignatureC13);
}

}

// The scopes a Microsoft name carries, innermost first: `?f@Inner@Outer@@...` gives Inner and
// Outer. False for anything this does not read - a template or a back-reference in a scope.
static bool scopesOf(const std::string &symbol, std::vector<std::string> &scopes) {
    if (symbol.size() < 2 || symbol[0] != '?') return false;
    std::size_t at;
    if (symbol[1] == '?') {
        if (symbol.size() < 4 || symbol[2] == '$') return false;
        at = symbol[2] == '_' ? 4 : 3;
    } else {
        at = symbol.find('@');
        if (at == std::string::npos) return false;
        at++;
    }
    while (at < symbol.size() && symbol[at] != '@') {
        const std::size_t end = symbol.find('@', at);
        if (end == std::string::npos) return false;
        const std::string piece = symbol.substr(at, end - at);
        if (piece.empty() || piece[0] == '?' || (piece.size() == 1 && piece[0] >= '0' && piece[0] <= '9'))
            return false;
        scopes.push_back(piece);
        at = end + 1;
    }
    return true;
}

std::string codeViewName(const std::string &symbol, const std::string &name, const Type *thisClass) {
    std::vector<std::string> scopes;
    std::string qualified;
    if (scopesOf(symbol, scopes)) {
        for (std::size_t i = scopes.size(); i-- > 0;) qualified += scopes[i] + "::";
    } else if (thisClass != nullptr && !thisClass->tag().empty()) {
        qualified = thisClass->displayTag() + "::";
    }
    return qualified + name;
}

std::string codeViewPath(const std::string &compDir, const std::string &name, bool escaped) {
    const bool absolute = (name.size() > 1 && name[1] == ':') ||
                          (!name.empty() && (name[0] == '/' || name[0] == '\\'));
    std::string full = absolute || compDir.empty() ? name : compDir + "\\" + name;
    std::string out;
    for (char c : full) {
        if (c == '/') c = '\\';
        if (escaped && (c == '\\' || c == '"')) out += '\\';
        out += c;
    }
    return out;
}

void writeCodeView(std::string &out, const Target &target, CodeViewLanguage language,
                   const std::string &objectName,
                   const std::vector<DwarfFunction> &fns,
                   const std::vector<DwarfGlobal> &globals, const Spell &spell) {
    if (fns.empty()) return;

    CodeViewTypes types(target);
    Records r(out, ".Lcv.");
    openDebugS(out, "");
    writeCompileUnit(r, language, objectName);
    for (std::size_t i = 0; i < fns.size(); i++)
        if (!fns[i].mergeable) writeFunction(r, fns[i], static_cast<int>(i), types, spell);

    // Each COMDAT function's records in a section that goes where its code goes - its funclets', which
    // follow it in `fns`, in the same section, as cl writes them: a second section on the same COMDAT is
    // one masm continues, and its signature word then sat among the records (measured, 1.29).
    std::string comdats;
    Records rc(comdats, ".Lcv.c.");
    for (std::size_t i = 0; i < fns.size(); i++) {
        if (!fns[i].mergeable || fns[i].funclet != 0) continue;
        openDebugS(comdats, fns[i].symbol);
        writeFunction(rc, fns[i], static_cast<int>(i), types, spell);
        for (std::size_t j = i + 1; j < fns.size() && fns[j].funclet != 0; j++)
            writeFunction(rc, fns[j], static_cast<int>(j), types, spell);
    }
    // After every function, so the S_UDT list holds every class they named.
    writeGlobals(r, globals, types, spell);
    line(out, "  .cv_filechecksums");
    line(out, "  .cv_stringtable");
    out += comdats;
    types.write(out);
}
