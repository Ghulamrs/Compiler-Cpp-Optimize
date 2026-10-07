#include "CodeView.h"
#include "CodeViewTypes.h"

#include <cctype>

namespace {

// Symbol record kinds (cvinfo.h) and the subsection that carries them.
const int kSObjName = 0x1101, kSCompile3 = 0x113c, kSFrameProc = 0x1012;
const int kSGProc32 = 0x1110, kSLProc32 = 0x110f, kSEnd = 0x0006;
const int kDebugSSymbols = 0xf1, kCvSignatureC13 = 4, kMachineAmd64 = 0xd0;
const int kSRegRel32 = 0x1111, kSBlock32 = 0x1103, kSUdt = 0x1108;
const int kSGData32 = 0x110d, kSLData32 = 0x110c, kCvAmd64Rbp = 334;

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
void writeObject(Records &r, const Local &l, int frameBias, CodeViewTypes &types, const Spell &spell) {
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
    num(o, "  .short", kCvAmd64Rbp);
    name(o, l.name);
    r.end();
}

bool declaresAnything(const DwarfFunction &f, int scope) {
    if (f.locals != nullptr)
        for (const Local &l : *f.locals)
            if (l.scope == scope) return true;
    for (std::size_t b = 1; b < f.blocks.size(); b++)
        if (f.blocks[b].parent == scope && declaresAnything(f, static_cast<int>(b))) return true;
    return false;
}

// **Parameters first and in order**: cdb counts off the procedure's arguments from the top.
void writeScope(Records &r, const DwarfFunction &f, int scope, CodeViewTypes &types, const Spell &spell) {
    if (f.locals != nullptr) {
        for (const Local &l : *f.locals)
            if (l.scope == scope && l.isParam) writeObject(r, l, f.frameBias, types, spell);
        for (const Local &l : *f.locals)
            if (l.scope == scope && !l.isParam) writeObject(r, l, f.frameBias, types, spell);
    }
    std::string &o = r.out();
    for (std::size_t b = 1; b < f.blocks.size(); b++) {
        const DwarfBlock &block = f.blocks[b];
        const int id = static_cast<int>(b);
        if (block.parent != scope || !declaresAnything(f, id)) continue;
        if (block.begin.empty() || block.end.empty()) {
            writeScope(r, f, id, types, spell);
            continue;
        }
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
    num(o, "  .long", procedureType(f, types));
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

    writeScope(r, f, 0, types, spell);

    r.begin(kSEnd);
    r.end();
    r.closeSubsection();
    line(o, "  .cv_linetable " + std::to_string(id) + ", " + f.symbol + ", " + f.codeEnd);
}

// The file's globals and statics, and a S_UDT for every class, union and enum named.
void writeGlobals(Records &r, const std::vector<DwarfGlobal> &globals, CodeViewTypes &types,
                  const Spell &spell) {
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

std::string codeViewPath(const std::string &compDir, const std::string &name) {
    const bool absolute = (name.size() > 1 && name[1] == ':') ||
                          (!name.empty() && (name[0] == '/' || name[0] == '\\'));
    std::string full = absolute || compDir.empty() ? name : compDir + "\\" + name;
    std::string out;
    for (char c : full) {
        if (c == '/') c = '\\';
        if (c == '\\' || c == '"') out += '\\';
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

    // Each COMDAT function's records in a section that goes where its code goes.
    std::string comdats;
    Records rc(comdats, ".Lcv.c.");
    for (std::size_t i = 0; i < fns.size(); i++) {
        if (!fns[i].mergeable) continue;
        openDebugS(comdats, fns[i].symbol);
        writeFunction(rc, fns[i], static_cast<int>(i), types, spell);
    }
    // After every function, so the S_UDT list holds every class they named.
    writeGlobals(r, globals, types, spell);
    line(out, "  .cv_filechecksums");
    line(out, "  .cv_stringtable");
    out += comdats;
    types.write(out);
}
