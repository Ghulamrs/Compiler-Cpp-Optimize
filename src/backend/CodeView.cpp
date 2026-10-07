#include "CodeView.h"

#include <cctype>

namespace {

// Symbol record kinds (cvinfo.h) and the subsection that carries them.
const int kSObjName = 0x1101, kSCompile3 = 0x113c, kSFrameProc = 0x1012;
const int kSGProc32 = 0x1110, kSLProc32 = 0x110f, kSEnd = 0x0006;
const int kDebugSSymbols = 0xf1, kCvSignatureC13 = 4, kMachineAmd64 = 0xd0;

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
    explicit Records(std::string &o) : o_(o) {}

    void begin(int kind) {
        const std::string n = std::to_string(++count_);
        end_ = ".Lcv.r" + n + ".e";
        const std::string b = ".Lcv.r" + n + ".b";
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
        subEnd_ = ".Lcv.s" + n + ".e";
        const std::string b = ".Lcv.s" + n + ".b";
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

void writeFunction(Records &r, const DwarfFunction &f, int id) {
    std::string &o = r.out();
    r.openSubsection();
    r.begin(f.external ? kSGProc32 : kSLProc32);
    line(o, "  .long 0, 0, 0");
    line(o, "  .long " + f.codeEnd + "-" + f.symbol);
    line(o, "  .long " + f.prologEnd + "-" + f.symbol);
    line(o, "  .long " + f.codeEnd + "-" + f.symbol);
    num(o, "  .long", 0);
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

    r.begin(kSEnd);
    r.end();
    r.closeSubsection();
    line(o, "  .cv_linetable " + std::to_string(id) + ", " + f.symbol + ", " + f.codeEnd);
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
                   const std::vector<DwarfGlobal> &globals) {
    (void)target;
    (void)globals;
    if (fns.empty()) return;

    Records r(out);
    openDebugS(out, "");
    writeCompileUnit(r, language, objectName);
    for (std::size_t i = 0; i < fns.size(); i++)
        if (!fns[i].mergeable) writeFunction(r, fns[i], static_cast<int>(i));
    line(out, "  .cv_filechecksums");
    line(out, "  .cv_stringtable");

    // Each COMDAT function's records in a section that goes where its code goes.
    for (std::size_t i = 0; i < fns.size(); i++) {
        if (!fns[i].mergeable) continue;
        openDebugS(out, fns[i].symbol);
        writeFunction(r, fns[i], static_cast<int>(i));
    }
}
