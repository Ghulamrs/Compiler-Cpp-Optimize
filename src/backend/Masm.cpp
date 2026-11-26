#include "Masm.h"

#include "../Mangle.h"
#include "../Name.h"
#include "../optimizer/OptIr.h"
#include "CodeView.h"

#include "../Source.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ostream>
#include <set>
#include <string>
#include <vector>

namespace {

const char kMasmNameChars[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_?@$";

[[noreturn]] void give_up(const std::string &what, const std::string &why) {
    std::fprintf(stderr, "%s: masm: %s\n  for: %s\n", program::kName, why.c_str(),
                 what.c_str());
    compileFailed();
}

bool isReservedInMasm(const std::string &name) {
    static const char *const kReserved[] = {
        "ah","al","ax","eax","rax","bh","bl","bx","ebx","rbx",
        "ch","cl","cx","ecx","rcx","dh","dl","dx","edx","rdx",
        "si","esi","rsi","di","edi","rdi","bp","ebp","rbp","sp","esp","rsp",
        "r8","r9","r10","r11","r12","r13","r14","r15",
        "cs","ds","es","fs","gs","ss","st","flat","eip","rip",
        "byte","word","dword","qword","tbyte","oword","ptr","offset",
        "length","lengthof","size","sizeof","type","typedef","this",
        "near","far","short","proc","endp","segment","ends","assume",
        "public","extern","externdef","end","align","even","org","dup",
        "mask","width","low","high","lowword","highword",
        "and","or","xor","not","mod","shl","shr","eq","ne","lt","le","gt","ge",
        "if","else","endif","macro","endm","rept","irp","exitm","local",
        "label","comment","include","includelib","name","group","record",
        "struc","struct","union","db","dw","dd","dq","dt","page","title",

        "aaa","aad","aam","aas","adc","add","bound","bsf","bsr","bt","btc",
        "btr","bts","call","cbw","cdq","clc","cld","cli","cmc","cmp","cmps",
        "cmpsb","cmpsd","cmpsw","cqo","cwd","cwde","daa","das","dec","div",
        "enter","esc","hlt","idiv","imul","in","inc","ins","int","into","iret",
        "ja","jae","jb","jbe","jc","jcxz","je","jg","jge","jl","jle","jmp",
        "jna","jnb","jnc","jne","jng","jnl","jno","jnp","jns","jnz","jo","jp",
        "jpe","jpo","js","jz","lahf","lds","lea","leave","les","lock","lods",
        "lodsb","lodsd","lodsw","loop","loope","loopne","loopnz","loopz",
        "mov","movs","movsb","movsd","movss","movsw","movsx","movsxd","movzx",
        "mul","neg","nop","out","outs","pop","popa","popf","push","pusha",
        "pushf","rcl","rcr","rep","repe","repne","repnz","repz","ret","retf",
        "retn","rol","ror","sahf","sal","sar","sbb","scas","scasb","scasd",
        "scasw","seta","setae","setb","setbe","sete","setg","setge","setl",
        "setle","setna","setne","setnz","setz","sgdt","shld","shrd","sidt",
        "stc","std","sti","stos","stosb","stosd","stosw","sub","test","wait",
        "xadd","xchg","xlat","xlatb",
        "fabs","fadd","fchs","fcos","fdiv","fild","finit","fist","fld","fmul",
        "fnop","fprem","fptan","frndint","fscale","fsin","fsqrt","fst","fstp",
        "fsub","ftst","fwait","fxam","fxch",
        "addpd","addps","addsd","addss","andpd","andps","cvtsd2ss","cvtsi2sd",
        "cvtss2sd","cvttsd2si","divsd","divss","maxsd","minsd","movapd","movaps",
        "movd","movq","mulsd","mulss","orpd","orps","pxor","sqrtsd","subsd",
        "subss","ucomisd","ucomiss","xorpd","xorps",
        nullptr,
    };
    std::string lower;
    for (char c : name) lower += static_cast<char>(std::tolower(c));
    for (const char *const *p = kReserved; *p != nullptr; ++p)
        if (lower == *p) return true;
    return false;
}

// **The reserved words this spelling writes as keywords** (registers, sizes,
// directives, the instructions it spells); any other a symbol is named (`fabs`,
// `div`) is given up by `OPTION NOKEYWORD:<...>`, its instructions as bytes.
bool writtenAsKeyword(const std::string &lower) {
    static const char *const kWritten[] = {
        "ah","al","ax","eax","rax","bh","bl","bx","ebx","rbx",
        "ch","cl","cx","ecx","rcx","dh","dl","dx","edx","rdx",
        "si","esi","rsi","di","edi","rdi","bp","ebp","rbp","sp","esp","rsp",
        "r8","r9","r10","r11","r12","r13","r14","r15",
        "cs","ds","es","fs","gs","ss","flat","rip",
        "byte","word","dword","qword","tbyte","oword","ptr","offset",
        "proc","endp","segment","ends","public","extern","externdef","end","align",
        "label","db","dw","dd","dq","dt",
        "add","and","call","cdq","cmp","cqo","dec","imul","inc","ja","jae","jb","jbe","je",
        "jg","jge","jl","jle","jmp","jne","jno","jnp","jns","jnz","jo","jp","js","jz","lea",
        "mov","movsd","movss","movsx","movsxd","movzx","neg","nop","not","or","pop","push",
        "rep","ret","sar","seta","setae","setb","setbe","sete","setg","setge","setl","setle",
        "setne","setnz","setz","shl","shr","sub","test","xor",
        "addsd","addss","cvtsd2ss","cvtsi2sd","cvtss2sd","cvttsd2si","divsd","divss",
        "movapd","movaps","movd","movq","mulsd","mulss","pxor","subsd","subss","ucomisd",
        "ucomiss","xorpd","xorps",
        nullptr,
    };
    for (const char *const *p = kWritten; *p != nullptr; ++p)
        if (lower == *p) return true;
    return false;
}

std::string lowered(const std::string &name) {
    std::string lower;
    for (char c : name) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lower;
}

bool canUnreserve(const std::string &name) {
    const std::string lower = lowered(name);
    return lower == "div" || lower == "idiv" || !writtenAsKeyword(lower);
}

// A general register as this spelling writes one - its number and width - or -1.
int registerNumber(const std::string &r, int *bits) {
    static const char *const k64[] = { "rax","rcx","rdx","rbx","rsp","rbp","rsi","rdi" };
    static const char *const k32[] = { "eax","ecx","edx","ebx","esp","ebp","esi","edi" };
    for (int i = 0; i < 8; i++) {
        if (r == k64[i]) { *bits = 64; return i; }
        if (r == k32[i]) { *bits = 32; return i; }
    }
    if (r.size() < 2 || r[0] != 'r' || !std::isdigit(static_cast<unsigned char>(r[1]))) return -1;
    std::size_t k = 1;
    int n = 0;
    while (k < r.size() && std::isdigit(static_cast<unsigned char>(r[k]))) n = n * 10 + (r[k++] - '0');
    if (n < 8 || n > 15) return -1;
    if (k == r.size()) { *bits = 64; return n; }
    if (r.substr(k) == "d") { *bits = 32; return n; }
    return -1;
}

const char *ptrFor(int bytes) {
    switch (bytes) {
    case 1: return "BYTE PTR ";
    case 2: return "WORD PTR ";
    case 4: return "DWORD PTR ";
    case 8: return "QWORD PTR ";
    default: return "";
    }
}

struct Rule {
    const char *att;
    const char *masm;
    int width;

};

const Rule kRules[] = {
    { "movb", "mov", 1 }, { "movw", "mov", 2 },
    { "movl", "mov", 4 }, { "movq", "mov", 8 },
    { "mov",  "mov", 0 }, { "movabs", "mov", 0 },
    { "movslq", "movsxd", 4 },
    { "movsbq", "movsx", 1 }, { "movswq", "movsx", 2 },

    { "movzbq", "movzx", 1 }, { "movzwq", "movzx", 2 },
    { "movzbl", "movzx", 1 }, { "movzwl", "movzx", 2 },

    { "lea", "lea", 0 },
    { "push", "push", 0 }, { "pop", "pop", 0 },

    { "add", "add", 0 }, { "sub", "sub", 0 }, { "imul", "imul", 0 },
    { "idiv", "idiv", 0 }, { "div", "div", 0 }, { "neg", "neg", 0 },
    { "and", "and", 0 }, { "or", "or", 0 }, { "xor", "xor", 0 },
    { "shl", "shl", 0 }, { "shr", "shr", 0 }, { "sar", "sar", 0 },
    { "cmp", "cmp", 0 }, { "cdq", "cdq", 0 }, { "cqo", "cqo", 0 },
    { "addl", "add", 4 }, { "cmpl", "cmp", 4 }, { "testb", "test", 1 },

    { "call", "call", 0 }, { "ret", "ret", 0 }, { "nop", "nop", 0 },
    { "jmp", "jmp", 0 }, { "je", "je", 0 }, { "jne", "jne", 0 },
    { "jae", "jae", 0 }, { "jns", "jns", 0 },

    { "sete", "sete", 0 }, { "setne", "setne", 0 },
    { "setl", "setl", 0 }, { "setle", "setle", 0 },
    { "setg", "setg", 0 }, { "setge", "setge", 0 },
    { "seta", "seta", 0 }, { "setae", "setae", 0 },
    { "setb", "setb", 0 }, { "setbe", "setbe", 0 },
    { "setp", "setp", 0 }, { "setnp", "setnp", 0 },

    { "movsd", "movsd", 8 }, { "movss", "movss", 4 },
    { "movapd", "movapd", 0 }, { "movaps", "movaps", 0 },
    { "movd", "movd", 0 },
    { "addsd", "addsd", 8 }, { "subsd", "subsd", 8 },
    { "mulsd", "mulsd", 8 }, { "divsd", "divsd", 8 },
    { "addss", "addss", 0 }, { "subss", "subss", 0 },
    { "mulss", "mulss", 0 }, { "divss", "divss", 0 },
    { "ucomisd", "ucomisd", 0 }, { "ucomiss", "ucomiss", 0 },
    { "pxor", "pxor", 0 }, { "xorpd", "xorpd", 0 }, { "xorps", "xorps", 0 },
    { "cvtsi2sdq", "cvtsi2sd", 8 }, { "cvtsi2ssq", "cvtsi2ss", 8 },
    { "cvttsd2si", "cvttsd2si", 0 }, { "cvttss2si", "cvttss2si", 0 },
    { "cvtss2sd", "cvtss2sd", 0 }, { "cvtsd2ss", "cvtsd2ss", 0 },

    // **What the optimizer writes as well as the walker**: every condition its
    // inversion can reach, and the widths its shortening pass narrows to.
    { "jz", "jz", 0 }, { "setz", "setz", 0 }, { "jnz", "jnz", 0 },
    { "setnz", "setnz", 0 }, { "jl", "jl", 0 }, { "jge", "jge", 0 },
    { "jle", "jle", 0 }, { "jg", "jg", 0 }, { "jb", "jb", 0 },
    { "jbe", "jbe", 0 }, { "ja", "ja", 0 }, { "js", "js", 0 },
    { "sets", "sets", 0 }, { "setns", "setns", 0 }, { "jp", "jp", 0 },
    { "jnp", "jnp", 0 }, { "jo", "jo", 0 }, { "seto", "seto", 0 },
    { "jno", "jno", 0 }, { "setno", "setno", 0 }, { "addb", "add", 1 },
    { "addw", "add", 2 }, { "addq", "add", 8 }, { "subb", "sub", 1 },
    { "subw", "sub", 2 }, { "subl", "sub", 4 }, { "subq", "sub", 8 },
    { "andb", "and", 1 }, { "andw", "and", 2 }, { "andl", "and", 4 },
    { "andq", "and", 8 }, { "orb", "or", 1 }, { "orw", "or", 2 },
    { "orl", "or", 4 }, { "orq", "or", 8 }, { "xorb", "xor", 1 },
    { "xorw", "xor", 2 }, { "xorl", "xor", 4 }, { "xorq", "xor", 8 },
    { "cmpb", "cmp", 1 }, { "cmpw", "cmp", 2 }, { "cmpq", "cmp", 8 },
    { "test", "test", 0 }, { "testw", "test", 2 }, { "testl", "test", 4 },
    { "testq", "test", 8 }, { "shlb", "shl", 1 }, { "shlw", "shl", 2 },
    { "shll", "shl", 4 }, { "shlq", "shl", 8 }, { "shrb", "shr", 1 },
    { "shrw", "shr", 2 }, { "shrl", "shr", 4 }, { "shrq", "shr", 8 },
    { "sarb", "sar", 1 }, { "sarw", "sar", 2 }, { "sarl", "sar", 4 },
    { "sarq", "sar", 8 }, { "negb", "neg", 1 }, { "negw", "neg", 2 },
    { "negl", "neg", 4 }, { "negq", "neg", 8 }, { "not", "not", 0 },
    { "notb", "not", 1 }, { "notw", "not", 2 }, { "notl", "not", 4 },
    { "notq", "not", 8 }, { "inc", "inc", 0 }, { "incb", "inc", 1 },
    { "incw", "inc", 2 }, { "incl", "inc", 4 }, { "incq", "inc", 8 },
    { "dec", "dec", 0 }, { "decb", "dec", 1 }, { "decw", "dec", 2 },
    { "decl", "dec", 4 }, { "decq", "dec", 8 }, { "rep movsq", "rep movsq", 0 },
};

const Rule *ruleFor(const std::string &m) {
    for (const Rule &r : kRules)
        if (m == r.att) return &r;
    return nullptr;
}

}

std::string MasmSpelling::mangle(const std::string &name) {
    // A name past ASCII - [lex.name]'s `caf\u00e9` - is not renamed: an import of one could not link.
    for (char c : name)
        if (static_cast<unsigned char>(c) >= 0x80)
            give_up(name, "a name past ASCII is not supported in the MASM spelling - no MASM "
                          "takes one, and the default GNU syntax, -masm=gnu, writes it");
    // A dot, or a character no MASM name holds (`<unnamed-type-$S1>`), only in an internal symbol.
    if (name.find_first_not_of(kMasmNameChars) != std::string::npos) {
        std::string out = "$";
        for (char c : name) out += std::strchr(kMasmNameChars, c) != nullptr ? c : '_';
        return out;
    }
    if (!isReservedInMasm(name)) return name;
    // A keyword this spelling never writes as one is given up, and the name is the C name - see
    // writtenAsKeyword (1.30). One it writes - a register, `test`, `add` - is renamed where this file
    // defines it, which only this file can then call, and said so.
    if (canUnreserve(name)) {
        unreserved_.insert(name);
        return name;
    }
    if (defined_.find(name) != defined_.end()) {
        if (renamed_.insert(name).second)
            std::fprintf(stderr, "%s: masm: warning: '%s' is a word MASM reserves and this file also "
                                 "writes as one, so it is defined as '$%s' - only this file can call "
                                 "it; the default GNU syntax, -masm=gnu, keeps the name\n",
                         program::kName, name.c_str(), name.c_str());
        return "$" + name;
    }
    give_up(name, "'" + name + "' is a name ml64 reserves and this file "
                  "emits, so an imported symbol cannot be spelled it");
}

MasmSpelling::Rendered MasmSpelling::render(const Op &x) {
    switch (x.kind) {
    case Op::Reg: {

        Str name = x.text.substr(1);
        return { std::string(name), false, false, name.startsWith("xmm") };
    }
    case Op::Imm: {
        if (!x.immNumeric) return { std::string(x.text), false, true, false };
        std::string v = x.immNeg ? "-" + std::to_string(x.uimm)
                                 : std::to_string(x.uimm);
        return { v, false, true, false };
    }
    case Op::Mem: {

        std::string t = "[" + std::string(x.text.substr(1));
        long long d = x.hasDisp ? x.disp : 0;
        // **The whole frame moved, so every offset into it moves with it.** This
        // target takes rbp after the allocation, exactly frameSize lower, so every
        // `[rbp + d]` becomes `[rbp + d + frameSize]` - the positive ones included.
        if (std::string(x.text.substr(1)) == "rbp") d += frameSize_;
        if (x.scale != 0) t += "+" + std::string(x.index.substr(1)) + "*" + std::to_string(x.scale);
        if (d != 0) {
            if (d < 0) t += std::to_string(d);
            else       t += "+" + std::to_string(d);
        }
        t += "]";
        return { t, true, false, false };
    }
    case Op::Rip: {

        std::string sym(x.text);
        referenced_.insert(sym);
        return { "[" + mangle(sym) + "]", true, false, false };
    }
    case Op::Ind:
        return { std::string(x.text.substr(1)), false, false, false };
    case Op::Lbl: {
        std::string sym(x.text);
        referenced_.insert(sym);
        return { mangle(sym), false, false, false };
    }
    // Sized here: ml64 cannot size `gs:[88]` from the register it is moved to (A2024, measured).
    case Op::Seg:
        return { "QWORD PTR " + std::string(x.text.substr(1)) + ":[" + std::to_string(x.disp) + "]", false, false, false };
    case Op::Tls: {
        // **ml64 relocates `[base + SECTIONREL x]` as SECREL** (measured with dumpbin); `lea eax,
        // SECTIONREL x` is REL32 and wrong. The project's own masm has no SECTIONREL operator yet.
        if (comdat_) give_up(std::string(x.text), "a thread_local is not supported yet in the project's own masm, "
                                                  "which has no SECTIONREL operator - the GNU spelling and ml64 have it");
        std::string sym(x.text);
        referencedData_.insert(sym);
        return { "[" + std::string(x.index.substr(1)) + " + SECTIONREL " + mangle(sym) + "]", true, false, false };
    }
    }
    return {};
}

void MasmSpelling::ins(const std::string &m) {
    afterCall_ = false;
    const Rule *r = ruleFor(m);
    if (r == nullptr) give_up(m, "an instruction this spelling does not know");
    o_ += "  "; o_ += r->masm; o_ += '\n';
}

void MasmSpelling::ins(const std::string &m, const Op &a) {
    afterCall_ = m == "call";
    const Rule *r = ruleFor(m);
    if (r == nullptr) give_up(m, "an instruction this spelling does not know");
    Rendered x = render(a);
    std::string t = x.text;
    if (x.isMem && r->width != 0) t = std::string(ptrFor(r->width)) + t;
    o_ += "  "; o_ += r->masm; o_ += ' '; o_ += t; o_ += '\n';
}

void MasmSpelling::ins(const std::string &m, const Op &a, const Op &b) {
    afterCall_ = false;
    // **The project's masm writes no segment override**: the thread's TLS array, cl's `mov rcx, gs:[58h]`,
    // goes in as its bytes - 65 48 8B 0C 25 58 00 00 00 for rcx - the register in ModRM's reg field (Phase 6).
    if (comdat_ && m == "mov" && a.kind == Op::Seg && b.kind == Op::Reg) {
        static const char *const regs[16] = { "%rax", "%rcx", "%rdx", "%rbx", "%rsp", "%rbp", "%rsi", "%rdi",
                                              "%r8", "%r9", "%r10", "%r11", "%r12", "%r13", "%r14", "%r15" };
        int n = -1;
        for (int i = 0; i < 16; i++) if (std::string(b.text) == regs[i]) n = i;
        if (n < 0 || std::string(a.text) != "%gs") give_up(m, "a segment load this spelling cannot write as bytes");
        const unsigned d = static_cast<unsigned>(a.disp);
        const unsigned bytes[9] = { 0x65, 0x48u | (n >= 8 ? 4u : 0u), 0x8B, 0x04u | ((n & 7u) << 3), 0x25,
                                    d & 0xFF, (d >> 8) & 0xFF, (d >> 16) & 0xFF, (d >> 24) & 0xFF };
        o_ += "  DB ";
        for (int i = 0; i < 9; i++) {
            char hex[8];
            std::snprintf(hex, sizeof hex, "0%02XH", bytes[i]);
            o_ += hex;
            o_ += i < 8 ? ", " : "\n";
        }
        return;
    }
    const Rule *r = ruleFor(m);
    if (r == nullptr) give_up(m, "an instruction this spelling does not know");

    Rendered src = render(a), dst = render(b);

    std::string name = r->masm;
    if (m == "movq" && (src.isXmm || dst.isXmm)) name = "movq";

    std::string sTxt = src.text, dTxt = dst.text;
    if (r->width != 0) {
        if (src.isMem) sTxt = std::string(ptrFor(r->width)) + sTxt;
        else if (dst.isMem && m != "movsxd")
            dTxt = std::string(ptrFor(r->width)) + dTxt;
    }

    if (r->width == 0 && dst.isMem && src.isImm)
        give_up(m, "a store of an immediate with no width to infer");

    o_ += "  "; o_ += name; o_ += ' '; o_ += dTxt; o_ += ", "; o_ += sTxt; o_ += '\n';
}

void MasmSpelling::defLabel(const std::string &l) {
    if (seg_ == Code) {
        // Internal: the Walker's own (.L.), and under -g the function's bounds (.Lfunc.) and its
        // blocks' (.L.blk.), which the CodeView records measure (1.29).
        if (l.compare(0, 2, ".L") != 0)
            give_up(l, "a label in code that is not an internal one");
        defined_.insert(l);
        o_ += mangle(l); o_ += ":\n";
        return;
    }
    defined_.insert(l);
    flushPending();
    pending_ = mangle(l);
}

// `mergeable` is what a COMDAT would be told, and ml64 has no directive that
// reaches the COMDAT bit - see CoffSpelling, which exists for that reason.
void MasmSpelling::functionBegin(const std::string &name, bool exported,
                                 bool mergeable) {
    inFunction_ = true;
    lastLoc_.clear();
    lastLocFile_ = lastLocLine_ = 0;
    linesRun_.clear();
    defined_.insert(name);
    if (exported) exported_.insert(name);
    flushPending();
    closeDataBlock();
    if (seg_ != Code) { o_ += "\n.CODE\n"; seg_ = Code; }
    mergeable_ = comdat_ && mergeable;
    if (mergeable_) o_ += ".text$mn SEGMENT ALIGN(16) 'CODE' COMDAT(" + mangle(name) + ")\n";

    // **`PROC`, not `PROC FRAME`, and the unwind data written by hand.** No MASM
    // directive reaches the handler *data* inside UNWIND_INFO, and cl writes
    // $pdata$ and $unwind$ itself. Done for every function, so one path is walked.
    fnName_ = name;
    o_ += mangle(name); o_ += " PROC\n";
    o_ += "$LNbeg$"; o_ += mangle(name); o_ += ":\n";
}

// See Spelling::stateLabel; cl writes `npad 1` here.
void MasmSpelling::stateLabel(const std::string &l) {
    if (afterCall_) ins("nop");
    defLabel(l);
}

void MasmSpelling::raw(const std::string &text) {
    flushPending();
    o_ += text;
}

void MasmSpelling::prologue(int frameSize, const std::string &lsda, int outgoing, int realign) {
    frameSize += outgoing;
    afterCall_ = false;
    // **The LSDA name says a landing pad exists, which is not the same
    // question.** A Microsoft FuncInfo follows only where one is written, and
    // the code generator says so through `noteHasEh` before `functionEnd`.
    (void)lsda;
    frameSize_ = frameSize;
    const std::string m = mangle(fnName_);

    // **The frame pointer is taken *after* the allocation on this target**, which
    // is the whole difference from the Itanium prologue: every displacement in an
    // FH3 table is an unsigned offset up from the establisher. Measured against cl.
    o_ += "  push rbp\n";
    o_ += "$LNpush$" + m + ":\n";
    // rbx as a second pushed nonvolatile, the realignment after the prologue - see CoffSpelling.
    if (realign > 0) o_ += "  push rbx\n$LNpushb$" + m + ":\n";
    // A frame of a page or more is probed first - cl's rule, measured: 4056 is
    // a plain sub, 4120 calls __chkstk. Unprobed, the first write at the bottom
    // skips the guard page and is an access violation, not a stack overflow.
    if (frameSize >= 4096) {
        o_ += "  mov eax, "; appendNum(o_, frameSize);
        o_ += "\n  call __chkstk\n  sub rsp, rax\n";
        referenced_.insert("__chkstk");
    } else if (frameSize > 0) {
        o_ += "  sub rsp, "; appendNum(o_, frameSize); o_ += '\n';
    }
    o_ += "$LNalloc$" + m + ":\n";
    o_ += "  mov rbp, rsp\n";
    o_ += "$LNfp$" + m + ":\n";
    // **The callee-saved registers an optimizer took, stored up from rbp**, as
    // the COFF prologue does: each slot is `disp` below the frame's top.
    for (std::size_t i = 0; i < saves_.size(); ++i) {
        o_ += "  mov QWORD PTR [rbp+" + std::to_string(frameSize + saves_[i].disp) + "], " +
              saves_[i].reg.substr(1) + "\n";
        o_ += "$LNsave" + std::to_string(i) + "$" + m + ":\n";
    }
    o_ += "$LNprolog$" + m + ":\n";
    if (realign > 0) o_ += "  and rsp, " + std::to_string(-realign) + "\n  mov rbx, rsp\n";

    // Last instruction first, which is the order an unwinder undoes them in:
    // the saves, SET_FPREG, the allocation, the push.
    unwindCodes_ = 0;
    unwindData_.clear();
    // UWOP_SAVE_NONVOL is 4 with the register in the high nibble, and the
    // slot's offset up from rsp - which rbp equals - in eights.
    for (std::size_t i = saves_.size(); i-- > 0;) {
        const int id = opt::parseReg(saves_[i].reg).id;
        char op[8];
        std::snprintf(op, sizeof op, "0%02XH", (id << 4) | 4);
        unwindData_ += "  DB $LNsave" + std::to_string(i) + "$" + m + "-$LNbeg$" + m + "\n";
        unwindData_ += std::string("  DB ") + op + "\n";
        unwindData_ += "  DW " + std::to_string((frameSize + saves_[i].disp) / 8) + "\n";
        unwindCodes_ += 2;
    }
    // UWOP_SET_FPREG is 3; the frame offset lives in the header, and it is
    // zero because rbp is set to rsp exactly.
    unwindData_ += "  DB $LNfp$" + m + "-$LNbeg$" + m + "\n";
    unwindData_ += "  DB 03H\n";
    unwindCodes_ += 1;
    // UWOP_ALLOC_SMALL is 2, with (size/8 - 1) in the high nibble, and
    // reaches 128 bytes; past that UWOP_ALLOC_LARGE is 1 with a slot of its
    // own holding size/8.
    if (frameSize > 0) {
        unwindData_ += "  DB $LNalloc$" + m + "-$LNbeg$" + m + "\n";
        if (frameSize <= 128 && frameSize % 8 == 0) {
            char buf[8];
            std::snprintf(buf, sizeof buf, "%02XH",
                          ((frameSize / 8 - 1) << 4) | 2);
            unwindData_ += "  DB 0"; unwindData_ += buf; unwindData_ += "\n";
            unwindCodes_ += 1;
        } else {
            unwindData_ += "  DB 01H\n";
            unwindData_ += "  DW " + std::to_string((frameSize + 7) / 8) + "\n";
            unwindCodes_ += 2;
        }
    }
    // UWOP_PUSH_NONVOL is 0 with the register in the high nibble - rbx is 3, rbp is 5.
    if (realign > 0) {
        unwindData_ += "  DB $LNpushb$" + m + "-$LNbeg$" + m + "\n  DB 030H\n";
        unwindCodes_ += 1;
    }
    unwindData_ += "  DB $LNpush$" + m + "-$LNbeg$" + m + "\n";
    unwindData_ += "  DB 050H\n";
    unwindCodes_ += 1;
}

// **Every offset here is a label difference, not a counted byte.** An unwind code
// records where its instruction *ends*, and `sub rsp, 40` is four bytes where
// `sub rsp, 400` is seven - the assembler chooses, so the assembler is asked.
void MasmSpelling::functionEnd(const std::string &name) {
    inFunction_ = false;
    const std::string m = mangle(name);
    // **The dot is the whole of it.** A segment called `pdata` is a segment called
    // pdata; the linker builds the image's exception directory from `.pdata`, and
    // without it the runtime finds no unwind record - measured with dumpbin.
    o_ += "$LNend$" + m + ":\n";
    o_ += m + " ENDP\n";
    if (mergeable_) o_ += ".text$mn ENDS\n";

    // READONLY and the alignment, or the linker finds two .pdata
    // sections with different attributes and says so.
    o_ += "\n.pdata SEGMENT READONLY ALIGN(4) 'DATA'" + associative() + "\n";
    o_ += "$pdata$" + m + " DD imagerel $LNbeg$" + m + "\n";
    o_ += "  DD imagerel $LNend$" + m + "\n";
    o_ += "  DD imagerel $unwind$" + m + "\n";
    o_ += ".pdata ENDS\n";

    // UNWIND_INFO.
    o_ += ".xdata SEGMENT READONLY ALIGN(8) 'DATA'" + associative() + "\n";
    // Version 1, and the flags in the top five bits. 0x19 is UNW_FLAG_EHANDLER and
    // UNW_FLAG_UHANDLER, which is what cl writes for a function with a `try`.
    // Without them the runtime unwinds past the frame and never reads the FuncInfo.
    o_ += "$unwind$" + m + (hasEh_ ? " DB 019H\n" : " DB 01H\n");
    o_ += "  DB $LNprolog$" + m + "-$LNbeg$" + m + "\n";
    o_ += "  DB " + std::to_string(unwindCodes_) + "\n";
    o_ += "  DB 05H\n";
    o_ += unwindData_;
    // The codes are padded to an even count, and the handler pointers go
    // after that padding rather than after the codes.
    if (unwindCodes_ % 2 != 0) o_ += "  DW 0\n";
    if (hasEh_) {
        referenced_.insert("__CxxFrameHandler3");
        o_ += "  DD imagerel __CxxFrameHandler3\n";
        o_ += "  DD imagerel $cppxdata$" + m + "\n";
    }
    o_ += ".xdata ENDS\n\n";
    o_ += ".CODE\n";

    unwindData_.clear();
    unwindCodes_ = 0;
    hasEh_ = false;
    saves_.clear();
}

// **-g through GRIDE's masm**: `.FILE n, "path"` names source file n and masm
// writes the line table from the `.LINE`s; the path comes as codeViewPath spelt
// it for clang, and a MASM string undoes that, doubling a quote instead.
void MasmSpelling::fileEntry(int n, const std::string &name) {
    std::string path;
    for (std::size_t i = 0; i < name.size(); i++) {
        if (name[i] == '\\' && i + 1 < name.size()) i++;
        if (name[i] == '"') path += '"';
        path += name[i];
    }
    o_ += ".FILE "; appendNum(o_, n); o_ += ", \""; o_ += path; o_ += "\"\n";
}

// Whether text holds an instruction: a line indented, and not a directive.
static bool holdsInstruction(const std::string &text) {
    for (std::size_t at = 0; at < text.size();) {
        if (text.compare(at, 2, "  ") == 0 && at + 2 < text.size() && text[at + 2] != '.') return true;
        const std::size_t nl = text.find('\n', at);
        if (nl == std::string::npos) break;
        at = nl + 1;
    }
    return false;
}

// `.LINE line, file` before the instructions of a line, by CoffSpelling::location's rules for
// `.cv_loc`: one entry per run of a line, no second run of it in a function (cdb refuses `bp` on
// a line entered twice), and one no instruction follows replaced by the next.
void MasmSpelling::location(int file, int line, int) {
    if (!inFunction_) return;
    if (file == lastLocFile_ && line == lastLocLine_) return;
    if (!linesRun_.insert(std::make_pair(file, line)).second) return;
    lastLocFile_ = file;
    lastLocLine_ = line;
    if (!lastLoc_.empty() && lastLocAt_ + lastLoc_.size() <= o_.size() &&
        o_.compare(lastLocAt_, lastLoc_.size(), lastLoc_) == 0 &&
        !holdsInstruction(o_.substr(lastLocAt_ + lastLoc_.size())))
        o_.erase(lastLocAt_, lastLoc_.size());
    lastLocAt_ = o_.size();
    o_ += "  .LINE "; appendNum(o_, line); o_ += ", "; appendNum(o_, file); o_ += '\n';
    lastLoc_ = o_.substr(lastLocAt_);
}

// **A funclet PROC's lines are a run of their own** (1.29): masm makes one table per PROC, so a line the
// function had may come again in its handler; the function's state waits and comes back after.
void MasmSpelling::funcletLinesBegin() {
    LineState s;
    s.lastLoc = lastLoc_; s.lastLocAt = lastLocAt_; s.lastLocFile = lastLocFile_; s.lastLocLine = lastLocLine_;
    s.linesRun = linesRun_;
    outerLines_.push_back(s);
    lastLoc_.clear(); lastLocAt_ = 0; lastLocFile_ = lastLocLine_ = 0;
    linesRun_.clear();
}

void MasmSpelling::funcletLinesEnd() {
    if (outerLines_.empty()) return;
    const LineState &s = outerLines_.back();
    lastLoc_ = s.lastLoc; lastLocAt_ = s.lastLocAt; lastLocFile_ = s.lastLocFile; lastLocLine_ = s.lastLocLine;
    linesRun_ = s.linesRun;
    outerLines_.pop_back();
}

// masm names no function in a `.LINE`: the PROC the instruction is in is the one described.
std::string MasmSpelling::locationDirective(int, int file, int line, int) const {
    std::string o = "  .LINE ";
    appendNum(o, line); o += ", "; appendNum(o, file); o += '\n';
    return o;
}

// Every name in an expression as this spelling writes it: `.Lcv.r3.e-.Lcv.r3.b` and the like.
std::string MasmSpelling::mangleNames(const std::string &expr) {
    std::string out;
    for (std::size_t i = 0; i < expr.size();) {
        const char c = expr[i];
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == '$' || c == '?' || c == '@') {
            std::size_t j = i;
            while (j < expr.size() && (std::isalnum(static_cast<unsigned char>(expr[j])) || expr[j] == '_' ||
                                       expr[j] == '.' || expr[j] == '$' || expr[j] == '?' || expr[j] == '@'))
                j++;
            out += mangle(expr.substr(i, j - i));
            i = j;
        } else {
            out += c;
            i++;
        }
    }
    return out;
}

// **writeCodeView's records for clang, in MASM**: the sections become SEGMENTs
// (a mergeable function's ASSOCIATIVE with its COMDAT), the data DD/DW/DB, the
// relocations SECTIONREL/SECTION; masm makes the line table from `.LINE`s.
void MasmSpelling::codeView(const std::string &gnu) {
    flushPending();
    closeDataBlock();
    std::string open;
    for (std::size_t at = 0; at < gnu.size();) {
        std::size_t nl = gnu.find('\n', at);
        if (nl == std::string::npos) nl = gnu.size();
        const std::string line = gnu.substr(at, nl - at);
        at = nl + 1;
        const std::size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        const std::string t = line.substr(b);
        if (b == 0 && t[t.size() - 1] == ':') {
            const std::string name = t.substr(0, t.size() - 1);
            defined_.insert(name);
            o_ += mangle(name); o_ += ":\n";
            continue;
        }
        const std::size_t sp = t.find_first_of(" \t");
        const std::string d = t.substr(0, sp);
        const std::string rest = sp == std::string::npos ? std::string() : t.substr(t.find_first_not_of(" \t", sp));
        if (d == ".section") {
            if (!open.empty()) o_ += open + " ENDS\n";
            open = rest.compare(0, 8, ".debug$T") == 0 ? ".debug$T" : ".debug$S";
            o_ += "\n" + open + " SEGMENT";
            const std::size_t assoc = rest.find(",associative,");
            if (assoc != std::string::npos) o_ += " ASSOCIATIVE(" + mangle(rest.substr(assoc + 13)) + ")";
            o_ += "\n";
        } else if (d == ".p2align") {
            o_ += "  ALIGN "; appendNum(o_, 1LL << std::atoi(rest.c_str())); o_ += '\n';
        } else if (d == ".long") {
            o_ += "  DD " + mangleNames(rest) + "\n";
        } else if (d == ".short") {
            o_ += "  DW " + mangleNames(rest) + "\n";
        } else if (d == ".byte") {
            o_ += "  DB " + rest + "\n";
        } else if (d == ".asciz") {
            // the GNU string's escapes undone, MASM's one escape - a doubled quote - made
            std::string s;
            for (std::size_t k = 1; k + 1 < rest.size(); k++) {
                if (rest[k] == '\\' && k + 2 < rest.size()) k++;
                if (rest[k] == '"') s += '"';
                s += rest[k];
            }
            o_ += s.empty() ? "  DB 0\n" : "  DB \"" + s + "\", 0\n";
        } else if (d == ".secrel32") {
            o_ += "  DD SECTIONREL " + mangleNames(rest) + "\n";
        } else if (d == ".secidx") {
            o_ += "  DW SECTION " + mangleNames(rest) + "\n";
        } else if (d != ".cv_linetable" && d != ".cv_filechecksums" && d != ".cv_stringtable") {
            give_up(line, "a CodeView directive the MASM spelling has no words for");
        }
    }
    if (!open.empty()) o_ += open + " ENDS\n";
    seg_ = None;  // the next code or data reopens its segment
}

void MasmCodeGen::writeDebug(const std::vector<DwarfFunction> &fns,
                             const std::vector<DwarfGlobal> &globals) {
    std::string gnu;
    writeCodeView(gnu, target_, CodeViewLanguage::Cpp,
                  codeViewPath(compDir(), lineSource()->files().front() + ".obj", false), fns, globals,
                  [](const std::string &s) { return s; });
    masm_.codeView(gnu);
}

void MasmSpelling::globl(const std::string &name) { exported_.insert(name); }

// A function's own name, or a second label in it, is covered by the COMDAT
// functionBegin opened.
void MasmSpelling::weakDefinition(const std::string &name) {
    if (!comdat_ || seg_ == Code) return;
    flushPending();
    closeDataBlock();
    pendingComdat_ = name;
}

// A segment of its own: a COMDAT's, or - with no COMDAT pending - one whose
// ALIGN() the simplified .DATA, .CONST and .DATA? (PARA, 16) cannot give an
// `alignas(64)` object; its name carries the alignment, since attributes may not change.
void MasmSpelling::openDataBlock(int align) {
    const bool comdat = !pendingComdat_.empty();
    // A COMDAT's block takes the classic name its section is made from: `.data` alone is the .DATA directive.
    std::string name = comdat ? (seg_ == Bss ? "_BSS" : seg_ == Data ? "_DATA" : "CONST")
                              : (seg_ == Bss ? ".bss" : seg_ == Data ? ".data" : ".rdata");
    if (!comdat) name += "$a" + std::to_string(align);
    o_ += std::string("\n") + name + " SEGMENT" + (seg_ == Const ? " READONLY" : "") +
          " ALIGN(" + std::to_string(align < 16 ? 16 : align) + ")" +
          (seg_ == Bss ? " 'BSS'" : " 'DATA'") +
          (comdat ? " COMDAT(" + mangle(pendingComdat_) + ")" : "") + "\n";
    pendingComdat_.clear();
    dataBlock_ = name;
    dataBlockUsed_ = false;
}

void MasmSpelling::closeDataBlock() {
    if (!pendingComdat_.empty()) openDataBlock(16);    // an object that never aligned
    if (!dataBlock_.empty()) o_ += dataBlock_ + " ENDS\n";
    dataBlock_.clear();
    dataBlockUsed_ = false;
}

void MasmSpelling::textSection() {
    flushPending();
    closeDataBlock();
    if (seg_ != Code) { o_ += "\n.CODE\n"; seg_ = Code; }
}

void MasmSpelling::rodataSection() {
    flushPending();
    closeDataBlock();
    if (seg_ != Const) { o_ += "\n.CONST\n"; seg_ = Const; }
}

void MasmSpelling::dataSection() {
    flushPending();
    closeDataBlock();
    if (seg_ != Data) { o_ += "\n.DATA\n"; seg_ = Data; }
}

void MasmSpelling::bssSection() {
    flushPending();
    closeDataBlock();
    if (seg_ != Bss) { o_ += "\n.DATA?\n"; seg_ = Bss; }
}

// **`.tls$`, with the dot**: ml64 names a `_TLS SEGMENT` `_TLS` and the linker builds no template
// from it - measured with dumpbin. The project's masm has no SECTIONREL to reach the object with.
void MasmSpelling::tlsDataSection() {
    flushPending();
    if (seg_ == Tls) return;
    closeDataBlock();
    o_ += "\n.tls$ SEGMENT ALIGN(16) 'DATA'\n";
    dataBlock_ = ".tls$";
    seg_ = Tls;
}

// `$tlsoff$x DD SECTIONREL x`, once per object this file reaches, in a segment of its own: the project's
// masm relocates SECTIONREL in a DD (the line tables' way) and in no operand.
void MasmSpelling::tlsOffsetWord(const std::string &symbol) {
    if (!tlsWords_.insert(symbol).second) return;
    referencedData_.insert(symbol);
    predefine({ "$tlsoff$" + symbol });
    trailer_ += "\n.rdata$tls SEGMENT READONLY ALIGN(4) 'DATA'\n" + mangle("$tlsoff$" + symbol) +
                " DD SECTIONREL " + mangle(symbol) + "\n.rdata$tls ENDS\n";
}

void MasmSpelling::tlsInitialiserEntry(const std::string &fn) {
    flushPending();
    closeDataBlock();
    o_ += "\n.CRT$XDU SEGMENT READONLY ALIGN(8) 'DATA'\n" + mangle(fn + "$initializer$") + " DQ ";
    o_ += mangle(fn);
    o_ += "\n.CRT$XDU ENDS\n";
    // The project's masm writes no INFO segment: a word naming __dyn_tls_init brings it into the link as
    // the directive does (Phase 6).
    if (comdat_) {
        referenced_.insert("__dyn_tls_init");
        o_ += ".rdata$tli SEGMENT READONLY ALIGN(8) 'DATA'\n" + mangle(fn + "$dyn_tls_init$") +
              " DQ __dyn_tls_init\n.rdata$tli ENDS\n";
    } else {
        o_ += ".drectve SEGMENT INFO\n  DB \" /include:__dyn_tls_init\"\n.drectve ENDS\n";
    }
    seg_ = None;
}

void MasmSpelling::objectType(const std::string &) {}
void MasmSpelling::objectSize(const std::string &, int) {}

// ALIGN 16, the code segment's own: MASM has no "pad only if it would cross" form.
void MasmSpelling::loopAlign(int) { flushPending(); o_ += "  ALIGN 16\n"; }

void MasmSpelling::align(int n) {
    flushPending();
    // The TLS template is one segment, every object in it: nothing to close or reopen.
    if (seg_ != Code && seg_ != Tls) {
        if (!dataBlock_.empty() && dataBlockUsed_) closeDataBlock();
        // An ALIGN past 16 inside .DATA is refused: such an object gets a segment of its own.
        if (!pendingComdat_.empty() || n > 16) openDataBlock(n);
        dataBlockUsed_ = true;
    }
    o_ += "  ALIGN "; appendNum(o_, n); o_ += '\n';
}

void MasmSpelling::zero(int n) {
    if (n == 0) return;
    items("DB", { std::to_string(n) +
                  (seg_ == Bss ? " DUP (?)" : " DUP (0)") });
}

void MasmSpelling::dataInt(int size, long long v) {
    const char *dir = size == 1 ? "DB" : size == 2 ? "DW"
                    : size == 4 ? "DD" : "DQ";
    items(dir, { std::to_string(v) });
}

void MasmSpelling::dataSym(const std::string &sym, long long off) {
    // **A symbol named by data is a use, and MASM needs it declared.**
    referenced_.insert(sym);
    std::string t = mangle(sym);
    if (off > 0) t += "+" + std::to_string(off);
    else if (off < 0) t += "-" + std::to_string(-off);
    items("DQ", { t });
}

void MasmSpelling::dataBytes(const std::string &bytes) {
    std::vector<std::string> it;
    it.reserve(bytes.size());
    for (char c : bytes)
        it.push_back(std::to_string(
            static_cast<int>(static_cast<unsigned char>(c))));
    items("DB", it);
}

void MasmSpelling::flushPending() {
    if (!pending_.empty()) {
        o_ += pending_; o_ += " LABEL BYTE\n";
        pending_.clear();
    }
}

void MasmSpelling::items(const char *dir, const std::vector<std::string> &it) {
    const std::size_t kPerLine = 16;
    for (std::size_t i = 0; i < it.size(); i += kPerLine) {
        std::string chunk;
        for (std::size_t j = i; j < it.size() && j < i + kPerLine; j++) {
            if (!chunk.empty()) chunk += ", ";
            chunk += it[j];
        }
        if (i == 0 && !pending_.empty()) {
            o_ += pending_; o_ += ' '; o_ += dir; o_ += ' '; o_ += chunk; o_ += '\n';
            pending_.clear();
        } else {
            o_ += "  "; o_ += dir; o_ += ' '; o_ += chunk; o_ += '\n';
        }
    }
}

// **`.CRT$XCU`, with the dot.** cl's listing writes `CRT$XCU SEGMENT` and
// `DQ FLAT:`, neither of which is what ml64 takes - the same trap `.pdata` and
// `.text$x` set. Measured on the box: this runs the function before main.
void MasmSpelling::flagExchange() {
    flushPending();
    closeDataBlock();
    if (seg_ != Code) { o_ += "\n.CODE\n"; seg_ = Code; }
    o_ += "__cxx1_flag_exchange PROC\n  mov eax, edx\n  xchg BYTE PTR [rcx], al\n  movzx eax, al\n  ret\n__cxx1_flag_exchange ENDP\n";
    defined_.insert("__cxx1_flag_exchange");
}

// Win64: the object's address in rcx, the value or the expected value's address in rdx, the desired value in r8.
// A byte or a word comes back zero-extended; `lock cmpxchg` leaves what the object held in the accumulator either
// way, and writing it back to *e when they were equal writes what *e had.
void MasmSpelling::atomicHelper(const std::string &name) {
    const AtomicHelper h = atomicHelperOf(name);
    if (h.op == AtomicHelper::None) return;
    flushPending();
    closeDataBlock();
    if (seg_ != Code) { o_ += "\n.CODE\n"; seg_ = Code; }
    static const char *const width[] = { "BYTE", "WORD", "DWORD", "QWORD" };
    static const char *const acc[] = { "al", "ax", "eax", "rax" };
    static const char *const desired[] = { "r8b", "r8w", "r8d", "r8" };
    const std::string object = std::string(width[h.k]) + " PTR [rcx]";
    const std::string expected = std::string(width[h.k]) + " PTR [rdx]";
    const char *const widen = h.k == 0 ? "  movzx eax, al\n" : h.k == 1 ? "  movzx eax, ax\n" : "";
    std::string body;
    switch (h.op) {
    case AtomicHelper::Load:
        body = h.k < 2 ? "  movzx eax, " + object + "\n" : std::string("  mov ") + acc[h.k] + ", " + object + "\n";
        break;
    case AtomicHelper::Xchg:
    case AtomicHelper::Add:
        body = h.k == 3 ? "  mov rax, rdx\n" : "  mov eax, edx\n";
        body += (h.op == AtomicHelper::Xchg ? "  xchg " : "  lock xadd ") + object + ", " + acc[h.k] + "\n" + widen;
        break;
    case AtomicHelper::Cas:
        body = std::string("  mov ") + acc[h.k] + ", " + expected + "\n  lock cmpxchg " + object + ", " + desired[h.k] +
               "\n  mov " + expected + ", " + acc[h.k] + "\n  sete al\n  movzx eax, al\n";
        break;
    case AtomicHelper::None:
        break;
    }
    o_ += name + " PROC\n" + body + "  ret\n" + name + " ENDP\n";
    defined_.insert(name);
}

void MasmSpelling::initialiserEntry(const std::string &fn, bool) {
    flushPending();
    closeDataBlock();
    o_ += "\n.CRT$XCU SEGMENT READONLY ALIGN(8) 'DATA'\n  DQ ";
    o_ += mangle(fn);
    o_ += "\n.CRT$XCU ENDS\n";
    seg_ = None;
}

void MasmSpelling::predefine(const std::vector<std::string> &names) {
    for (const std::string &n : names) defined_.insert(n);
}

// **`div` named a symbol is no instruction in this file** (1.30): NOKEYWORD gives the word up for the
// whole file, so every `div reg` and `idiv reg` the code generator wrote becomes its bytes - F7 /6 and
// F7 /7 on the register, REX.W for a 64-bit one and REX.B for r8-r15 - and keeps the text as a comment.
// **`dup` named a symbol** (2026-11-10, Microsoft's names - `dup` is POSIX's for _dup): NOKEYWORD gives the word up
// for the file, as it gives `div` up, so a zero fill this spelling wrote as `DB n DUP (0)` - or `(?)` in .DATA? - is
// written out instead, 32 bytes to a line. Only a file that names dup pays for it.
static void expandDupFills(std::vector<std::string> &chunks, const std::set<std::string> &unreserved)
{
    bool given = false;
    for (const std::string &u : unreserved) {
        std::string lower;
        for (char c : u) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == "dup") given = true;
    }
    if (!given) return;
    for (std::string &text : chunks) {
        std::string out;
        std::size_t at = 0;
        while (at < text.size()) {
            std::size_t nl = text.find('\n', at);
            const std::size_t end = nl == std::string::npos ? text.size() : nl + 1;
            const std::string line = text.substr(at, end - at);
            at = end;
            const std::size_t d = line.find(" DUP (");
            const std::size_t db = d == std::string::npos ? std::string::npos : line.rfind("DB ", d);
            if (db == std::string::npos) { out += line; continue; }
            const std::string head = line.substr(0, db);
            long long n = std::atoll(line.substr(db + 3, d - db - 3).c_str());
            const char *fill = line.compare(d + 6, 1, "?") == 0 ? "?" : "0";
            bool first = true;
            while (n > 0) {
                const long long k = n > 32 ? 32 : n;
                out += first ? head : std::string("  ");
                out += "DB ";
                for (long long i = 0; i < k; i++) { if (i != 0) out += ','; out += fill; }
                out += '\n';
                n -= k;
                first = false;
            }
        }
        text.swap(out);
    }
}
void MasmSpelling::finishText(std::vector<std::string> &chunks) {
    expandDupFills(chunks, unreserved_);
    bool given[2] = { false, false };
    for (const std::string &u : unreserved_) {
        const std::string lower = lowered(u);
        if (lower == "div") given[0] = true;
        if (lower == "idiv") given[1] = true;
    }
    if (!given[0] && !given[1]) return;
    for (std::string &text : chunks) {
        std::string out;
        std::size_t at = 0;
        while (at < text.size()) {
            std::size_t nl = text.find('\n', at);
            const std::size_t end = nl == std::string::npos ? text.size() : nl + 1;
            const std::string line = text.substr(at, end - at);
            at = end;
            const int which = line.compare(0, 6, "  div ") == 0 ? 0 : line.compare(0, 7, "  idiv ") == 0 ? 1 : -1;
            if (which < 0 || !given[which]) { out += line; continue; }
            std::string reg = line.substr(which == 0 ? 6 : 7);
            while (!reg.empty() && (reg.back() == '\n' || reg.back() == '\r' || reg.back() == ' ')) reg.pop_back();
            int bits = 0;
            const int n = registerNumber(reg, &bits);
            if (n < 0)
                give_up(line, "'" + std::string(which == 0 ? "div" : "idiv") + "' names a symbol in this "
                              "file, so its instruction is written as bytes, and these bytes are for a "
                              "64- or 32-bit register only");
            char bytes[64];
            const int rex = (bits == 64 ? 0x48 : 0x40) | (n >= 8 ? 1 : 0);
            const int modrm = 0xC0 | ((which == 0 ? 6 : 7) << 3) | (n & 7);
            if (rex != 0x40) std::snprintf(bytes, sizeof bytes, "  DB 0%02XH, 0F7H, 0%02XH", rex, modrm);
            else             std::snprintf(bytes, sizeof bytes, "  DB 0F7H, 0%02XH", modrm);
            out += bytes;
            out += "  ; ";
            out += which == 0 ? "div " : "idiv ";
            out += reg;
            out += '\n';
        }
        text.swap(out);
    }
}

void MasmSpelling::preamble(std::ostream &sink) {
    sink << "; Generated by cxx1 for x86_64-windows, in MASM syntax for ml64.\n";
    sink << "; The instruction selection is the same one the Linux backend makes;\n";
    sink << "; only the spelling is Microsoft's. See src/backend/Masm.cpp.\n\n";

    // **DOTNAME lets a segment be called `.pdata`, and NOSCOPED lets the unwind
    // data name the labels inside a procedure.** Both are needed for one reason:
    // the tables live outside the function and measure into it.
    sink << "OPTION DOTNAME\n";
    sink << "OPTION NOSCOPED\n";
    // **MASM exports every PROC unless told otherwise**, a `static` included.
    sink << "OPTION PROC:PRIVATE\n\n";

    for (const std::string &g : unreserved_)
        sink << "OPTION NOKEYWORD:<" << g << ">\n";
    if (!unreserved_.empty()) sink << "\n";

    for (const std::string &e : exported_) {
        const std::string spelt = mangle(e);
        // cl's `<unnamed-type-x>`: an object or a specialization of an unnamed class no other unit can name,
        // kept in this one under the respelt name rather than exported under one MASM cannot hold.
        if (e.find("<unnamed-type-") != std::string::npos) continue;
        // Renaming an exported name would break the link; only a dot was ever renamed in one.
        if (e.find_first_not_of(std::string(kMasmNameChars) + ".") != std::string::npos)
            give_up(e, "an exported name holds a character no MASM name can spell");
        sink << "PUBLIC " << spelt << "\n";
    }
    for (const std::string &r : referenced_)
        if (defined_.find(r) == defined_.end() && referencedData_.find(r) == referencedData_.end())
            sink << "EXTERN " << mangle(r) << ":PROC\n";
    for (const std::string &r : referencedData_)
        if (defined_.find(r) == defined_.end())
            sink << "EXTERN " << mangle(r) << ":BYTE\n";
    sink << "\n";
}

void MasmSpelling::postamble(std::ostream &sink) {

    if (!pending_.empty())
        give_up(pending_, "a data label left dangling at the end of the file");
    // The chunks are written by now, so a block still open is closed here.
    const std::size_t mark = o_.size();
    closeDataBlock();
    sink << o_.substr(mark);
    o_.resize(mark);
    sink << trailer_;
    trailer_.clear();
    sink << "\nEND\n";
}

// **The chain the Microsoft ABI wants before anything may be thrown** - ??_R0H@8,
// _CT??_R0H@84, _CTA1H, _TI1H - measured from cl's listing, `imagerel` throughout
// and in cl's segments. Then -2, the scratch word saying nothing is entered yet.
int MasmCodeGen::establisherOffset(int slot) const {
    return masm_.frameSize_ - slot;
}

// **Through the assembler rather than as a raw string**, so the renderer's `+ frameSize_` is the
// only place the frame moves: `[rbp-slot]` written here comes out as `[rbp + frameSize - slot]`
// there, which is the same arithmetic establisherOffset does for the tables.
void MasmCodeGen::storeUnwindHelp(int slot) {
    if (optimizing()) optimizer_->sharedSlot(-slot, 8);
    a_->ins("movq", imm(-2), mem(-slot, "%rbp"));
}

// **A funclet is a slice of the ordinary output, lifted**: where the handler's
// code began is remembered and cut back to, so the body is exact; one opened
// inside another is cut out of it first - the slices nest.
std::string MasmCodeGen::beginFunclet(std::size_t pos, int number, bool cleanup) {
    if (optimizing()) optimizer_->funcletBegin();
    atOutput([this] {
        masm_.raw("");                   // nothing pending inside the slice
        funcletMarks_.push_back(out_.size());
    });
    if (!funcletSymbol_.empty()) outerFunclets_.push_back(std::make_pair(funcletSymbol_, funcletEntryLoc_));
    funcletSymbol_ = masm_.mangledName() + funcletKind_ +
                     std::to_string(funcletIndex_++);
    funcletDebugBegin(funcletSymbol_, pos, number, cleanup);
    return funcletSymbol_;
}

// The funclet's own frame: `rdx` is the establisher frame, so adding the parent's
// frame size back recovers the rbp every local was written against, and a funclet
// *returns* where to carry on, in rax. A cleanup has nothing to return to.
void MasmCodeGen::endCleanupFunclet() {
    closeFunclet(std::string());
    funcletKind_ = "$catch$";
}

void MasmCodeGen::endFunclet(const std::string &resume) {
    closeFunclet("  lea rax, " + masm_.labelName(resume) + "\n");
}

// An early exit: the continuation in rax, then the funclet's epilogue.
void MasmCodeGen::funcletLeave(const std::string &label) {
    a_->ins("lea", rip(label), reg("%rax"));
    a_->ins("jmp", lbl("$LNleave$" + funcletSymbol_));
}

void MasmCodeGen::closeFunclet(const std::string &tail) {
    const std::string sym = funcletSymbol_;
    funcletDebugEnd();
    // Written here as raw text, so said to be defined: an early exit's `jmp $LNleave$` made it an EXTERN otherwise.
    masm_.predefine({ sym, "$LNbeg$" + sym, "$LNpush$" + sym, "$LNprolog$" + sym, "$LNleave$" + sym, "$LNend$" + sym });
    // The text around the body: the head, then the body as written out, then the tail with the unwind data.
    std::string head, f;
    // **`.text$x`, and the dot is the whole of it** - the same trap as `.pdata`. A
    // segment called `text` gets data attributes, so the handler faults at its own
    // first instruction; 'CODE' is what gives it execute permission beside .text.
    head += "\n.text$x SEGMENT ALIGN(16) 'CODE'" + masm_.associative() + "\n";
    head += sym + " PROC\n";
    head += "$LNbeg$" + sym + ":\n";
    // -g: the prologue is on the `catch` line, as cl's funclet begins (1.29).
    head += funcletEntryLoc_;
    head += "  mov QWORD PTR [rsp+16], rdx\n";
    head += "  push rbp\n";
    head += "$LNpush$" + sym + ":\n";
    head += "  sub rsp, 32\n";
    head += "$LNprolog$" + sym + ":\n";
    // **rdx is the establisher frame, and that is now exactly the parent's rbp** -
    // the two became one thing when the frame pointer moved to the bottom of the
    // allocation, so the handler reaches the parent's locals with no adjustment.
    head += "  mov rbp, rdx\n";
    f += tail;
    f += "$LNleave$" + sym + ":\n";
    f += "  add rsp, 32\n";
    f += "  pop rbp\n";
    f += "  ret 0\n";
    f += "$LNend$" + sym + ":\n";
    f += sym + " ENDP\n";
    f += ".text$x ENDS\n";

    // A funclet carries unwind data of its own, naming the same handler and *the
    // parent's* FuncInfo - the two share one description of the try. Its .pdata
    // goes to a pile written after every function, for the sorting reason recorded.
    masm_.trailer_ += "\n.pdata SEGMENT READONLY ALIGN(4) 'DATA'" + masm_.associative() + "\n";
    masm_.trailer_ += "$pdata$" + sym + " DD imagerel $LNbeg$" + sym + "\n";
    masm_.trailer_ += "  DD imagerel $LNend$" + sym + "\n";
    masm_.trailer_ += "  DD imagerel $unwind$" + sym + "\n";
    masm_.trailer_ += ".pdata ENDS\n";
    f += ".xdata SEGMENT READONLY ALIGN(8) 'DATA'" + masm_.associative() + "\n";
    f += "$unwind$" + sym + " DB 019H\n";
    f += "  DB $LNprolog$" + sym + "-$LNbeg$" + sym + "\n";
    f += "  DB 02H\n";
    f += "  DB 00H\n";
    // UWOP_ALLOC_SMALL for 32 bytes: the code is 2 with (size/8 - 1) in the
    // high nibble, so (32/8 - 1) = 3 gives 032H. 042H would claim 40 and
    // leave the runtime unwinding through this funclet to the wrong place.
    f += "  DB $LNprolog$" + sym + "-$LNbeg$" + sym + "\n";
    f += "  DB 032H\n";
    f += "  DB $LNpush$" + sym + "-$LNbeg$" + sym + "\n";
    f += "  DB 050H\n";
    f += "  DD imagerel __CxxFrameHandler3\n";
    f += "  DD imagerel $cppxdata$" + masm_.mangledName() + "\n";
    f += ".xdata ENDS\n";
    // **The body is lifted out of the output where it was written**, which
    // with an optimizer in front is after the function it belongs to.
    atOutput([this, head, f] {
        masm_.raw("");
        const std::string body = out_.substr(funcletMarks_.back());
        out_.resize(funcletMarks_.back());
        funcletMarks_.pop_back();
        funclets_ += head + body + f;
    });
    if (optimizing()) optimizer_->funcletEnd();
    funcletSymbol_.clear();
    if (!outerFunclets_.empty()) {
        funcletSymbol_ = outerFunclets_.back().first;
        funcletEntryLoc_ = outerFunclets_.back().second;
        outerFunclets_.pop_back();
    }
}

void MasmCodeGen::emitExceptionTables(const Function &fn) {
    if (msStates().empty() && fn.msUnwindHelp() == 0) {
        if (!callSites().empty()) emitLsda(fn.symbol(), false);
        out_ += funclets_;
        funclets_.clear();
        funcletIndex_ = 0;
        return;
    }

    const std::string m = masm_.mangledName();
    const std::vector<MsState> &states = msStates();
    const std::vector<MsTryBlock> &tries = msTryBlocks();
    std::string o;
    o += funclets_;
    funclets_.clear();
    funcletIndex_ = 0;

    // **Every offset is measured from the establisher frame**, so a slot at
    // [rbp-N] is at frameSize-N; a cleanup is a state whose action is its funclet.
    // Associative with an inline function, or a copy the linker discards would leave these naming its labels.
    o += "\n.xdata SEGMENT READONLY ALIGN(8) 'DATA'" + masm_.associative() + "\n";
    o += "$cppxdata$" + m + " DD 019930522H\n";
    o += "  DD " + std::to_string(states.size()) + "\n";
    o += states.empty() ? "  DD 00H\n" : "  DD imagerel $stateUnwindMap$" + m + "\n";
    o += "  DD " + std::to_string(tries.size()) + "\n";
    o += tries.empty() ? "  DD 00H\n" : "  DD imagerel $tryMap$" + m + "\n";
    const std::vector<MsIpRow> ip = msIpTable();
    o += "  DD " + std::to_string(1 + ip.size()) + "\n";
    o += "  DD imagerel $ip2state$" + m + "\n";
    o += "  DD " + std::to_string(establisherOffset(msUnwindHelpSlot())) + "\n";
    o += "  DD 00H\n";                      // no exception specification
    // EHFlags: compiled with /EHsc, and 4 for a noexcept function, which the runtime ends the program in.
    o += "  DD 0" + std::to_string(1 | (fn.isNoexcept() ? 4 : 0)) + "H\n";

    if (!states.empty()) o += "$stateUnwindMap$" + m;
    for (std::size_t k = 0; k < states.size(); k++) {
        o += (k == 0 ? " DD " : "  DD ");
        o += (states[k].toState < 0 ? std::string("0ffffffffH")
                                    : std::to_string(states[k].toState)) + "\n";
        o += states[k].action.empty() ? "  DD 00H\n"
                                      : "  DD imagerel " + states[k].action + "\n";
    }

    if (!tries.empty()) o += "$tryMap$" + m;
    for (std::size_t k = 0; k < tries.size(); k++) {
        o += (k == 0 ? " DD " : "  DD ") + std::to_string(tries[k].tryLow) + "\n";
        o += "  DD " + std::to_string(tries[k].tryHigh) + "\n";
        o += "  DD " + std::to_string(tries[k].catchHigh) + "\n";
        o += "  DD " + std::to_string(tries[k].handlers.size()) + "\n";
        o += "  DD imagerel $handlerMap$" + std::to_string(k) + "$" + m + "\n";
    }

    for (std::size_t k = 0; k < tries.size(); k++) {
        o += "$handlerMap$" + std::to_string(k) + "$" + m;
        for (std::size_t i = 0; i < tries[k].handlers.size(); i++) {
            const MsHandlerRow &h = tries[k].handlers[i];
            // 0x40 is HT_IsCatchAll, and a catch-all names no type; 0x08 is
            // HT_IsReference, which tells the runtime to put the object's
            // address in the slot rather than a copy of it. Measured.
            o += (i == 0 ? " DD " : "  DD ");
            o += h.descriptor.empty() ? "040H\n"
                                      : std::to_string((h.byReference ? 8 : 0) |
                                                       (h.constPointer ? 1 : 0)) + "\n";
            o += h.descriptor.empty() ? "  DD 00H\n"
                                      : "  DD imagerel " + h.descriptor + "\n";
            o += "  DD " + std::to_string(h.objectSlot == 0
                                              ? 0 : establisherOffset(h.objectSlot)) + "\n";
            o += "  DD imagerel " + h.funclet + "\n";
            // dispFrame: the parent's saved frame pointer, [rsp+16]; cl 038H
            o += "  DD 56\n";
        }
    }

    // Where each state begins, in address order; a catch funclet is entered in
    // its own handler state.
    o += "$ip2state$" + m + " DD imagerel $LNbeg$" + m + "\n";
    o += "  DD 0ffffffffH\n";
    for (const MsIpRow &r : ip) {
        o += "  DD imagerel " + (r.entry ? r.label : masm_.labelName(r.label)) + "\n";
        o += "  DD " + (r.state < 0 ? std::string("0ffffffffH") : std::to_string(r.state)) + "\n";
    }
    o += ".xdata ENDS\n\n.CODE\n";
    out_ += o;
}

// **Where each RTTI or throw record begins.** For the project's assembler it is a COMDAT of its own
// and public, so the linker keeps one copy where every unit that named the type wrote one - the choice
// the COFF path makes in coffRecord. For ml64, which cannot say COMDAT, the records share one plain segment: `first` opens it, and the others follow in it.
std::string MasmCodeGen::record(const char *segment, int align, const std::string &name,
                                bool first, bool writable) {
    // ml64 refuses a segment reopened with another ALIGN (A2015), and .xdata$x is opened at 8 and at 4.
    if (!masm_.comdat() && std::string(segment) == ".xdata$x") align = 8;
    const std::string open = std::string(segment) + (writable ? " SEGMENT ALIGN(" : " SEGMENT READONLY ALIGN(") +
                             std::to_string(align) + ") 'DATA'";
    if (!masm_.comdat()) return first ? open + "\n" : std::string();
    masm_.globl(name);
    return (first ? std::string() : std::string(segment) + " ENDS\n") +
           open + " COMDAT(" + name + ")\n";
}

// **The five objects the Microsoft ABI wants before it will answer a
// `dynamic_cast`**, measured from clang.
void MasmCodeGen::emitClassRtti(const Program &program) {
    if (program.rtti.empty()) return;
    std::string &o = out_;

    // Every class named and every base its array lists, closed and deduplicated: a
    // base needs the full set of records even when nothing in this file names it.
    std::vector<const Type *> all;
    for (std::size_t i = 0; i < program.rtti.size(); i++) {
        int attributes = 0;
        for (const MicrosoftBaseClass &r : program.msRttiRowsOf(program.rtti[i], &attributes)) {
            bool had = false;
            for (std::size_t j = 0; j < all.size(); j++) if (all[j] == r.cls) had = true;
            if (!had) all.push_back(r.cls);
        }
    }

    std::set<std::string> bases;
    for (std::size_t i = 0; i < all.size(); i++) {
        MicrosoftRtti n;
        std::string why;
        if (!microsoftClassRttiNames(all[i], &n, &why)) continue;
        int attributes = 0;
        const std::vector<MicrosoftBaseClass> rows = program.msRttiRowsOf(all[i], &attributes);

        // **The descriptor is writable** - its spare word caches the undecorated name - so
        // `.data$r`, which the linker folds into .data: a segment named `.data` is MASM's
        // .DATA directive and stops the file. The four records below it are `.rdata$r`.
        o += record(".data$r", 16, n.descriptor, true, true);
        o += n.descriptor + " DQ ??_7type_info@@6B@\n";
        o += "  DQ 0\n";
        o += "  DB '" + n.decorated + "', 00H\n";
        o += ".data$r ENDS\n";
        msDescriptors_.insert(n.descriptor);

        // Where each class of the array sits inside this one, and what it is there: one record per
        // place and attributes, `??_R1BA@?0A@EA@I2@@8`, shared by every array that lists it.
        bool first = true;
        for (const MicrosoftBaseClass &r : rows) {
            if (!bases.insert(r.name).second) continue;
            MicrosoftRtti b;
            if (!microsoftClassRttiNames(r.cls, &b, &why)) continue;
            o += record(".rdata$r", 4, r.name, first);
            first = false;
            o += r.name + " DD imagerel " + b.descriptor + "\n";
            o += "  DD " + std::to_string(r.contained) + "\n";
            o += "  DD " + std::to_string(r.mdisp) + "\n";
            o += "  DD " + std::to_string(r.pdisp) + "\n";   // pdisp: -1 outside a virtual base
            o += "  DD " + std::to_string(r.vdisp) + "\n";
            o += "  DD " + std::to_string(r.attributes) + "\n";
            o += "  DD imagerel " + b.hierarchy + "\n";
        }

        o += record(".rdata$r", 4, n.array, first);
        o += n.array;
        for (std::size_t k = 0; k < rows.size(); k++)
            o += (k == 0 ? " DD imagerel " : "  DD imagerel ") + rows[k].name + "\n";
        o += "  DD 00H\n";

        o += record(".rdata$r", 4, n.hierarchy, false);
        o += n.hierarchy + " DD 00H\n";
        o += "  DD " + std::to_string(attributes) + "\n";    // 1: a class here has two bases
        o += "  DD " + std::to_string(rows.size()) + "\n";
        o += "  DD imagerel " + n.array + "\n";

        // The locator names itself, which is how the runtime recovers the image
        // base every other field is relative to. One per vftable, at its vfptr.
        std::vector<Program::MsLocator> locators;
        for (const Program::MsLocator &l : program.msLocators)
            if (l.cls == all[i]) locators.push_back(l);
        if (locators.empty()) locators.push_back(Program::MsLocator{ all[i], n.locator, 0 });
        for (const Program::MsLocator &l : locators) {
            o += record(".rdata$r", 4, l.name, false);
            o += l.name + " DD 01H\n";
            o += "  DD " + std::to_string(l.offset) + "\n";   // the vfptr's offset in the object
            o += "  DD " + std::to_string(l.cdOffset) + "\n";
            o += "  DD imagerel " + n.descriptor + "\n";
            o += "  DD imagerel " + n.hierarchy + "\n";
            o += "  DD imagerel " + l.name + "\n";
        }
        o += ".rdata$r ENDS\n";
    }
}

void MasmCodeGen::emitThrowInfo(const Program &program) {
    if (program.msThrows.empty()) return;
    std::string &o = out_;
    std::set<std::string> catchables, arrays;
    for (std::size_t i = 0; i < program.msThrows.size(); i++) {
        const MicrosoftThrow &n = program.msThrows[i];
        for (std::size_t k = 0; k < n.catchables.size(); k++) {
            const MicrosoftThrow::Catchable &c = n.catchables[k];
            // **cl's listing writes `FLAT:` here and ml64 rejects it.** That prefix is
            // 32-bit MASM's way of naming a flat-model address; the 64-bit assembler
            // has no such keyword, so the listing records what cl means.
            if (msDescriptors_.insert(c.descriptor).second) {
                o += record(".data$r", 16, c.descriptor, true, true);
                o += c.descriptor + " DQ ??_7type_info@@6B@\n";
                o += "  DQ 0\n";
                o += "  DB '" + c.decorated + "', 00H\n";
                o += ".data$r ENDS\n";
            }
            if (!n.thrown || !catchables.insert(c.name).second) continue;
            o += record(".xdata$x", 8, c.name, true);
            o += c.name + (c.simple ? " DD 01H\n" : " DD 00H\n");
            o += "  DD imagerel " + c.descriptor + "\n";
            o += "  DD " + std::to_string(c.mdisp) + "\n";
            o += "  DD 0ffffffffH\n";
            o += "  DD 00H\n";
            o += "  DD " + std::to_string(c.size) + "\n";
            o += c.copyCtor.empty() ? "  DD 00H\n" : "  DD imagerel " + c.copyCtor + "\n";
            o += ".xdata$x ENDS\n";
        }
        if (!n.thrown) continue;
        // **One array per name.** `int *` and `const int *` thrown in one unit have two ThrowInfos -
        // the const is the info's - over one `_CTA2PEAH`, a throw's record dropping the pointee's
        // const; the second copy was an assembler error (1.28, 2026-10-22, review of 1.25, item 9).
        const bool arrayHere = arrays.insert(n.array).second;
        if (arrayHere) {
            o += record(".xdata$x", 4, n.array, true);
            o += n.array + " DD " + std::to_string(n.catchables.size()) + "\n";
            for (std::size_t k = 0; k < n.catchables.size(); k++)
                o += "  DD imagerel " + n.catchables[k].name + "\n";
        }
        // The info follows the array in its segment, or opens one of its own where the array was
        // written with an earlier info.
        o += record(".xdata$x", 4, n.info, !arrayHere);
        o += n.info + (n.isConst ? " DD 01H\n" : " DD 00H\n");
        o += n.destructor.empty() ? "  DD 00H\n" : "  DD imagerel " + n.destructor + "\n";
        o += "  DD 00H\n";
        o += "  DD imagerel " + n.array + "\n";
        o += ".xdata$x ENDS\n";
    }
}

// The ThrowInfo names are defined in this file, so the spelling must not put
// them in the EXTERN list it writes for everything a call mentions.
void MasmCodeGen::run(const Program &program) {
    std::vector<std::string> mine;
    for (std::size_t i = 0; i < program.msThrows.size(); i++) {
        const MicrosoftThrow &n = program.msThrows[i];
        if (n.thrown) mine.push_back(n.info);
        for (std::size_t k = 0; k < n.catchables.size(); k++)
            mine.push_back(n.catchables[k].descriptor);
    }
    // **And every class descriptor this file defines**, for the same reason: the spelling writes an
    // EXTERN for each name a call mentions, and these are mentioned by the `lea` in front of every
    // __RTDynamicCast while being laid down a few lines above it.
    for (std::size_t i = 0; i < program.rtti.size(); i++) {
        int attributes = 0;
        for (const MicrosoftBaseClass &r : program.msRttiRowsOf(program.rtti[i], &attributes)) {
            MicrosoftRtti n;
            std::string why;
            if (!microsoftClassRttiNames(r.cls, &n, &why)) break;
            // **All five, not the descriptor alone.** The descriptor is the
            // one a `lea` mentions, so it was the only one that had to be here
            // while only instructions recorded a reference.
            mine.push_back(n.descriptor);
            mine.push_back(n.baseDescriptor);
            mine.push_back(r.name);
            mine.push_back(n.array);
            mine.push_back(n.hierarchy);
            bool listed = false;
            for (const Program::MsLocator &l : program.msLocators) listed = listed || l.cls == r.cls;
            if (!listed) mine.push_back(n.locator);
        }
    }
    for (const Program::MsLocator &l : program.msLocators) mine.push_back(l.name);
    masm_.predefine(mine);
    // **Before the code, not after it.** The base run() flushes what it has
    // built when it finishes, so anything appended afterwards goes to a buffer
    // nobody reads.
    if (!program.msThrows.empty() || !program.rtti.empty())
        out_ += "\nEXTRN ??_7type_info@@6B@:QWORD\n";
    // **A pure virtual's slot names a routine no object file here defines.**
    // GNU as takes an undeclared symbol and leaves it to the linker; ml64 will
    // not, and answers `A2006: undefined symbol : _purecall`.
    for (std::size_t g = 0; g < program.globals.size(); g++) {
        bool found = false;
        const std::vector<GlobalPiece> &pieces = program.globals[g].init;
        for (std::size_t i = 0; i < pieces.size(); i++)
            if (pieces[i].symbol == "_purecall") { found = true; break; }
        if (found) { out_ += "\nEXTRN _purecall:PROC\n"; break; }
    }
    emitClassRtti(program);
    emitThrowInfo(program);
    X86_64Linux::run(program);
}
