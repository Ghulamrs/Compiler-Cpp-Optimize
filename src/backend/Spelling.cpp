#include "Spelling.h"

#include "../optimizer/OptIr.h"

#include <ostream>
#include <string>

void GnuSpelling::op(const Op &x) {
    switch (x.kind) {
    case Op::Reg: o_ += x.text; return;
    case Op::Imm:
        o_ += '$';
        if (!x.immNumeric) { o_ += x.text; return; }
        if (x.immNeg) o_ += '-';
        appendNum(o_, x.uimm);
        return;
    case Op::Mem:
        if (x.hasDisp) appendNum(o_, x.disp);
        o_ += '(';
        o_ += x.text;
        if (x.scale != 0) { o_ += ','; o_ += x.index; o_ += ','; appendNum(o_, x.scale); }
        o_ += ')';
        return;
    case Op::Rip: o_ += sym(std::string(x.text.p, x.text.n)); o_ += "(%rip)"; return;
    case Op::Ind: o_ += '*'; o_ += x.text; return;
    case Op::Lbl: o_ += sym(std::string(x.text.p, x.text.n)); return;
    case Op::Seg: o_ += x.text; o_ += ':'; appendNum(o_, x.disp); return;
    // Local-exec, as clang: the offset from the thread pointer (linker's)
    case Op::Tls: o_ += sym(std::string(x.text.p, x.text.n)); o_ += "@TPOFF("; o_ += x.index; o_ += ')'; return;
    }
}

void GnuSpelling::ins(const std::string &m) {
    afterCall_ = false;
    o_ += "  "; o_ += m; o_ += '\n';
}

void GnuSpelling::ins(const std::string &m, const Op &a) {
    afterCall_ = m == "call";
    o_ += "  "; o_ += m; o_ += ' ';
    op(a);
    o_ += '\n';
}

void GnuSpelling::ins(const std::string &m, const Op &a, const Op &b) {
    afterCall_ = false;
    o_ += "  "; o_ += m; o_ += ' ';
    op(a);
    o_ += ", ";
    op(b);
    o_ += '\n';
}

void GnuSpelling::defLabel(const std::string &l) { o_ += sym(l); o_ += ":\n"; }

// **A mergeable function is a COMDAT group of its own**, as gcc and clang
// write one: `.weak` alone leaves every unit's copy in .text, where the linker
// keeps it; a group is kept once. Measured on Compiler++ (docs/PORT-FROM-CPPI.md).
void GnuSpelling::functionBegin(const std::string &name, bool exported,
                                bool mergeable) {
    if (exported && !(mergeable && weakBindsGlobal())) globl(name);
    textSection_ = mergeable
        ? "  .section .text." + sym(name) + ",\"axG\",@progbits," + sym(name) + ",comdat\n"
        : std::string("  .text\n");
    textSection();
    // **A function starts on an even address**: the Itanium member pointer keeps
    // "virtual" in a code word's low bit, and a function at an odd address read
    // as a vtable offset. Two bytes is the ABI's need; 16 is a speed choice (S10).
    o_ += "  .p2align 1\n";
    defLabel(name);
}

// **Unwind data, and it is the same three directives in every function.** A
// cxx1 frame has one shape, so the CFA is rbp + 16 throughout; without it a
// backtrace stops here and no exception passes. MASM has always said this.
void GnuSpelling::prologue(int frameSize, const std::string &lsda, int outgoing, int realign) {
    frameSize += outgoing;
    afterCall_ = false;
    o_ += "  .cfi_startproc\n";
    if (!lsda.empty()) {
        o_ += "  .cfi_personality 155, DW.ref.__gxx_personality_v0\n";
        o_ += "  .cfi_lsda 27, " + lsda + "\n";
    }
    ins("push", reg("%rbp"));
    o_ += "  .cfi_def_cfa_offset 16\n";
    o_ += "  .cfi_offset %rbp, -16\n";
    ins("mov", reg("%rsp"), reg("%rbp"));
    o_ += "  .cfi_def_cfa_register %rbp\n";
    // The CFA stays rbp + 16 through a realignment, so the unwinder is told only where rbx went.
    if (realign > 0) {
        ins("push", reg("%rbx"));
        o_ += "  .cfi_offset %rbx, -24\n";
        ins("and", imm(-realign), reg("%rsp"));
    }
    if (frameSize > 0) ins("sub", imm(frameSize), reg("%rsp"));
    if (realign > 0) ins("mov", reg("%rsp"), reg("%rbx"));
    // The canonical frame address is rbp + 16.
    for (const SavedReg &s : saves_) {
        ins("mov", reg(s.reg), mem(s.disp, "%rbp"));
        o_ += "  .cfi_offset " + s.reg + ", " + std::to_string(s.disp - 16) + "\n";
    }
}

void GnuSpelling::functionEnd(const std::string &) {
    o_ += "  .cfi_endproc\n";
    saves_.clear();
}

void GnuSpelling::globl(const std::string &name) {
    o_ += "  .globl "; o_ += sym(name); o_ += '\n';
}

// `.weak` is what makes the linker fold the copies of an inline function rather than reject them, and
// it binds the symbol global by itself: clang writes no `.globl` beside it, and its assembler warns of one.
void GnuSpelling::weakDefinition(const std::string &name) {
    o_ += "  .weak "; o_ += sym(name); o_ += '\n';
}

void GnuSpelling::fileEntry(int n, const std::string &name) {
    o_ += "  .file ";
    appendNum(o_, n);
    o_ += " \"";
    o_ += name;
    o_ += "\"\n";
}

void GnuSpelling::location(int file, int line, int column) {
    o_ += "  .loc ";
    appendNum(o_, file);
    o_ += ' ';
    appendNum(o_, line);
    o_ += ' ';
    appendNum(o_, column);
    o_ += '\n';
}

// Measured from clang: the function's address in .init_array, and
// __dso_handle declared hidden where the file hands it to __cxa_atexit.
void GnuSpelling::initialiserEntry(const std::string &fn, bool dsoHandle) {
    if (dsoHandle) o_ += "  .hidden __dso_handle\n";
    o_ += "  .section .init_array,\"aw\",@init_array\n  .p2align 3, 0x0\n  .quad ";
    o_ += sym(fn);
    o_ += '\n';
}

void GnuSpelling::flagExchange() {
    o_ += "  .text\n__cxx1_flag_exchange:\n  movl %esi, %eax\n  xchgb %al, (%rdi)\n  movzbl %al, %eax\n  retq\n";
}

// <atomic>'s interlocked operations in AT&T's spelling, for the System V registers (rdi, rsi, rdx) or Win64's
// (rcx, rdx, r8): what MasmSpelling::atomicHelper writes, instruction for instruction.
static std::string gnuAtomicHelper(const std::string &name, bool win64) {
    const Spelling::AtomicHelper h = Spelling::atomicHelperOf(name);
    if (h.op == Spelling::AtomicHelper::None) return std::string();
    static const char *const suffix[] = { "b", "w", "l", "q" };
    static const char *const acc[] = { "%al", "%ax", "%eax", "%rax" };
    static const char *const desiredSysV[] = { "%dl", "%dx", "%edx", "%rdx" };
    static const char *const desiredWin[] = { "%r8b", "%r8w", "%r8d", "%r8" };
    const std::string object = win64 ? "(%rcx)" : "(%rdi)";
    const std::string expected = win64 ? "(%rdx)" : "(%rsi)";
    const std::string value = h.k == 3 ? (win64 ? "%rdx" : "%rsi") : (win64 ? "%edx" : "%esi");
    const char *const desired = (win64 ? desiredWin : desiredSysV)[h.k];
    const char *const widen = h.k == 0 ? "  movzbl %al, %eax\n" : h.k == 1 ? "  movzwl %ax, %eax\n" : "";
    const std::string s = suffix[h.k];
    std::string body;
    switch (h.op) {
    case Spelling::AtomicHelper::Load:
        body = h.k == 0 ? "  movzbl " + object + ", %eax\n" : h.k == 1 ? "  movzwl " + object + ", %eax\n"
             : "  mov" + s + " " + object + ", " + acc[h.k] + "\n";
        break;
    case Spelling::AtomicHelper::Xchg:
    case Spelling::AtomicHelper::Add:
        body = (h.k == 3 ? "  movq " : "  movl ") + value + (h.k == 3 ? ", %rax\n" : ", %eax\n");
        body += (h.op == Spelling::AtomicHelper::Xchg ? "  xchg" : "  lock xadd") + s + " " + acc[h.k] + ", " + object + "\n" + widen;
        break;
    case Spelling::AtomicHelper::Cas:
        body = "  mov" + s + " " + expected + ", " + acc[h.k] + "\n  lock cmpxchg" + s + " " + desired + ", " + object +
               "\n  mov" + s + " " + acc[h.k] + ", " + expected + "\n  sete %al\n  movzbl %al, %eax\n";
        break;
    case Spelling::AtomicHelper::None:
        break;
    }
    return "  .text\n" + name + ":\n" + body + "  retq\n";
}

void GnuSpelling::atomicHelper(const std::string &name) { o_ += gnuAtomicHelper(name, false); }

void GnuSpelling::textSection()   { o_ += textSection_; }
void GnuSpelling::rodataSection() { o_ += "  .section .rodata\n"; }
void GnuSpelling::relroSection() { o_ += "  .section .data.rel.ro,\"aw\"\n"; }
void GnuSpelling::dataSection()   { o_ += "  .data\n"; }
void GnuSpelling::bssSection()    { o_ += "  .bss\n"; }
// Measured from clang: the T flag is what makes a section part of the TLS template.
void GnuSpelling::tlsDataSection() { o_ += "  .section .tdata,\"awT\",@progbits\n"; }
void GnuSpelling::tlsBssSection()  { o_ += "  .section .tbss,\"awT\",@nobits\n"; }

void GnuSpelling::objectType(const std::string &name) {
    o_ += "  .type "; o_ += sym(name); o_ += ", @object\n";
}

void GnuSpelling::objectSize(const std::string &name, int size) {
    o_ += "  .size "; o_ += sym(name); o_ += ", "; appendNum(o_, size); o_ += '\n';
}

// A label between the call and this one has the same address, so only an
// instruction clears the mark.
void GnuSpelling::stateLabel(const std::string &l) {
    if (afterCall_) ins("nop");
    defLabel(l);
}

void GnuSpelling::align(int n) { o_ += "  .align "; appendNum(o_, n); o_ += '\n'; }
// `.p2align 6,,L-1` pads to the next 64-byte line exactly when the L bytes from here would cross one.
void GnuSpelling::loopAlign(int bytes) { o_ += "  .p2align 6,,"; appendNum(o_, bytes - 1); o_ += '\n'; }
void GnuSpelling::zero(int n)  { o_ += "  .zero ";  appendNum(o_, n); o_ += '\n'; }

void GnuSpelling::dataInt(int size, long long v) {
    switch (size) {
    case 1: o_ += "  .byte "; break;
    case 2: o_ += "  .word "; break;
    case 4: o_ += "  .long "; break;
    default: o_ += "  .quad "; break;
    }
    appendNum(o_, v);
    o_ += '\n';
}

void GnuSpelling::dataSym(const std::string &s, long long off) {
    o_ += "  .quad ";
    o_ += sym(s);
    if (off > 0) { o_ += '+'; appendNum(o_, off); }
    else if (off < 0) { o_ += '-'; appendNum(o_, -off); }
    o_ += '\n';
}

void GnuSpelling::dataBytes(const std::string &bytes) {
    o_ += "  .byte ";
    for (std::size_t k = 0; k < bytes.size(); k++) {
        if (k) o_ += ", ";
        appendNum(o_, static_cast<long long>(
                          static_cast<unsigned char>(bytes[k])));
    }
    o_ += '\n';
}


// --- CoffSpelling -----------------------------------------------------------

// Quoted where GNU-as would not take the name as an identifier. A Microsoft
// mangled name always carries a '?' or an '@'; a C name carries neither, so
// `printf` and `__CxxFrameHandler3` are written plainly, as clang writes them.
std::string CoffSpelling::sym(const std::string &name) const {
    std::string n = name;
    // **A `.L` label is a temporary, and a table cannot name one.**
    if (n.compare(0, 2, ".L") == 0) {
        std::string out = "$";
        for (char c : n) out += (c == '.') ? '_' : c;
        n = out;
    }
    if (n.find('?') == std::string::npos && n.find('@') == std::string::npos &&
        n.find('$') == std::string::npos)
        return n;
    return "\"" + n + "\"";
}

void CoffSpelling::functionBegin(const std::string &name, bool exported,
                                 bool mergeable) {
    fnName_ = name;
    mergeable_ = mergeable;
    if (mergeable) {
        // The section carries the COMDAT bit and names the symbol it folds on;
        // `discard` is IMAGE_COMDAT_SELECT_ANY, which is what an inline
        // definition wants - keep one, drop the rest.
        o_ += "  .section .text,\"xr\",discard," + sym(name) + "\n";
        opened_ = name;
    } else {
        textSection();
    }
    if (exported) globl(name);
    defLabel(name);
    if (codeView_) { cvCurrent_ = cvFunctions_++; o_ += "  .cv_func_id "; appendNum(o_, cvCurrent_); o_ += '\n'; }
}

// **A funclet's lines are a function's own** (1.29): the next id, declared where the funclet's text begins
// (it moves to `.text$x` with it), and a fresh run of lines; the function's state waits and comes back.
void CoffSpelling::funcletLinesBegin() {
    if (!codeView_) return;
    LineState s;
    s.current = cvCurrent_; s.lastLoc = lastLoc_; s.lastLocAt = lastLocAt_;
    s.lastLocFn = lastLocFn_; s.lastLocFile = lastLocFile_; s.lastLocLine = lastLocLine_;
    s.linesRun = linesRun_;
    outerLines_.push_back(s);
    cvCurrent_ = cvFunctions_++;
    o_ += "  .cv_func_id "; appendNum(o_, cvCurrent_); o_ += '\n';
    lastLoc_.clear(); lastLocAt_ = 0; lastLocFn_ = -1; lastLocFile_ = lastLocLine_ = 0;
    linesRun_.clear();
}

void CoffSpelling::funcletLinesEnd() {
    if (!codeView_ || outerLines_.empty()) return;
    const LineState &s = outerLines_.back();
    cvCurrent_ = s.current; lastLoc_ = s.lastLoc; lastLocAt_ = s.lastLocAt;
    lastLocFn_ = s.lastLocFn; lastLocFile_ = s.lastLocFile; lastLocLine_ = s.lastLocLine;
    linesRun_ = s.linesRun;
    outerLines_.pop_back();
}

std::string CoffSpelling::locationDirective(int fn, int file, int line, int column) const {
    std::string o = "  .cv_loc ";
    appendNum(o, fn); o += ' '; appendNum(o, file); o += ' '; appendNum(o, line); o += ' '; appendNum(o, column);
    o += '\n';
    return o;
}

// The name arrives absolute and escaped (codeViewPath); the checksum is optional and left out.
void CoffSpelling::fileEntry(int n, const std::string &name) {
    codeView_ = true;
    o_ += "  .cv_file ";
    appendNum(o_, n);
    o_ += " \"" + name + "\"\n";
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

// **A line entry names its function**, which is the one functionBegin last numbered.
// One with no instruction after it is replaced: at one address cdb takes the first, so
// a breakpoint on `int y = ...` stopped and said it was on the function's opening line.

// **One entry per run of a line, as cl writes it**: a `for` gave four, a column apiece, and cdb
// refused `bp` on the line as ambiguous.
void CoffSpelling::location(int file, int line, int column) {
    if (cvCurrent_ < 0) return;
    if (cvCurrent_ == lastLocFn_ && file == lastLocFile_ && line == lastLocLine_) return;
    // **No second run of a line**: a loop's step written after its body was a second run of the `for`
    // line, and cdb refused `bp` on it as ambiguous; that code counts as the line before it.
    if (cvCurrent_ != lastLocFn_) linesRun_.clear();
    if (!linesRun_.insert(std::make_pair(file, line)).second) return;
    lastLocFn_ = cvCurrent_;
    lastLocFile_ = file;
    lastLocLine_ = line;
    if (!lastLoc_.empty() && lastLocAt_ + lastLoc_.size() <= o_.size() &&
        o_.compare(lastLocAt_, lastLoc_.size(), lastLoc_) == 0 &&
        !holdsInstruction(o_.substr(lastLocAt_ + lastLoc_.size())))
        o_.erase(lastLocAt_, lastLoc_.size());
    lastLocAt_ = o_.size();
    o_ += "  .cv_loc ";
    appendNum(o_, cvCurrent_);
    o_ += ' ';
    appendNum(o_, file);
    o_ += ' ';
    appendNum(o_, line);
    o_ += ' ';
    appendNum(o_, column);
    o_ += '\n';
    lastLoc_ = o_.substr(lastLocAt_);
}

// For a global the code generator says this *before* the label, which is where
// the section directive has to go; for a function functionBegin has already
// opened one, and a second would start an empty section.
void CoffSpelling::weakDefinition(const std::string &name) {
    if (opened_ == name) { opened_.clear(); return; }
    // **The COMDAT takes the plain section's own attributes**: a template's
    // static member and its guard are writable, and clang puts them in
    // `.bss,"bw",discard` - measured. Read-only only where the section is.
    const char *where = std::strstr(plainSection_, ".bss") != nullptr
                          ? ".bss,\"bw\""
                      : std::strstr(plainSection_, ".data") != nullptr
                          ? ".data,\"dw\""
                          : ".rdata,\"dr\"";
    o_ += "  .section " + std::string(where) + ",discard," + sym(name) + "\n";
    comdatData_ = 1;
}

// Measured from clang for x86_64-pc-windows-msvc: the same pointer, in the
// section the CRT walks before main. **`.CRT$XCU` carries no COMDAT clause**,
// and the `unique,0` that used to be here is why this is a comment.
void CoffSpelling::flagExchange() {
    o_ += "  .text\n__cxx1_flag_exchange:\n  movl %edx, %eax\n  xchgb %al, (%rcx)\n  movzbl %al, %eax\n  retq\n";
}

void CoffSpelling::atomicHelper(const std::string &name) { o_ += gnuAtomicHelper(name, true); }

void CoffSpelling::initialiserEntry(const std::string &fn, bool) {
    o_ += "  .section .CRT$XCU,\"dr\"\n  .p2align 3, 0x0\n  .quad ";
    o_ += sym(fn);
    o_ += '\n';
}

// COFF spells the read-only segment .rdata, and has no .type or .size. Each
// plain section is remembered, so a data COMDAT can be closed by putting the
// plain one back - see comdatData_.
void CoffSpelling::rodataSection() {
    plainSection_ = "  .section .rdata,\"dr\"\n"; comdatData_ = 0; o_ += plainSection_;
}
void CoffSpelling::dataSection() {
    plainSection_ = "  .data\n"; comdatData_ = 0; o_ += plainSection_;
}
void CoffSpelling::bssSection() {
    plainSection_ = "  .bss\n"; comdatData_ = 0; o_ += plainSection_;
}
// One section for both, as cl and clang write it: a zero-filled thread_local is written as zeros.
void CoffSpelling::tlsDataSection() {
    plainSection_ = "  .section .tls$,\"dw\"\n"; comdatData_ = 0; o_ += plainSection_;
}
// Measured from cl: an object with a thread_local initialiser carries `/include:__dyn_tls_init`
// in its directives, and one without does not; the CRT's per-thread callback is what runs .CRT$XDU.
void CoffSpelling::tlsInitialiserEntry(const std::string &fn) {
    o_ += "  .section .CRT$XDU,\"dr\"\n  .p2align 3, 0x0\n" + sym(fn + "$initializer$") + ":\n  .quad ";
    o_ += sym(fn);
    o_ += "\n  .section .drectve,\"yni\"\n  .ascii \" /include:__dyn_tls_init\"\n";
}
void CoffSpelling::align(int n) {
    // The COMDAT object's own align passes through; the next object's puts
    // the plain section back first, so it does not land in a section that is
    // somebody else's to discard.
    if (comdatData_ == 1) comdatData_ = 2;
    else if (comdatData_ == 2) { o_ += plainSection_; comdatData_ = 0; }
    GnuSpelling::align(n);
}
void CoffSpelling::objectType(const std::string &name) { (void)name; }
void CoffSpelling::objectSize(const std::string &name, int size) {
    (void)name; (void)size;
}

// **Hand-written, because `.seh_handlerdata` cannot live in a COMDAT.** The `.seh_*` directives are the tidy way and the assembler builds .pdata and .xdata from them - measured, and it works for every function in plain .text.
void CoffSpelling::prologue(int frameSize, const std::string &lsda, int outgoing, int realign) {
    // The area goes under everything, so it is simply more frame: every
    // offset below is measured up from rsp, which rbp equals.
    frameSize += outgoing;
    afterCall_ = false;
    hasEh_ = !lsda.empty();
    frameSize_ = frameSize;
    const std::string b = "\"$LNbeg$" + fnName_ + "\"";
    o_ += b + ":\n";

    // The frame pointer is taken after the allocation on this target, so every
    // FH3 displacement is an unsigned offset up from the establisher.
    o_ += "  push %rbp\n";
    o_ += "\"$LNpush$" + fnName_ + "\":\n";
    // rbx a second pushed nonvolatile; the `and` comes after the prologue, where the unwinder, recovering rsp from rbp, never sees it (clang-cl).
    if (realign > 0) { o_ += "  push %rbx\n\"$LNpushb$" + fnName_ + "\":\n"; }
    // A frame of a page or more is probed through __chkstk first, as the
    // MASM prologue does and for the same measured reason.
    if (frameSize >= 4096) {
        o_ += "  mov $"; appendNum(o_, frameSize);
        o_ += ", %eax\n  call __chkstk\n  sub %rax, %rsp\n";
    } else if (frameSize > 0) {
        o_ += "  sub $"; appendNum(o_, frameSize); o_ += ", %rsp\n";
    }
    o_ += "\"$LNalloc$" + fnName_ + "\":\n";
    o_ += "  mov %rsp, %rbp\n";
    o_ += "\"$LNfp$" + fnName_ + "\":\n";
    for (std::size_t i = 0; i < saves_.size(); ++i) {
        ins("mov", reg(saves_[i].reg), mem(saves_[i].disp, "%rbp"));
        o_ += "\"$LNsave" + std::to_string(i) + "$" + fnName_ + "\":\n";
    }
    o_ += "\"$LNprolog$" + fnName_ + "\":\n";
    if (realign > 0) { o_ += "  and $"; appendNum(o_, -realign); o_ += ", %rsp\n  mov %rsp, %rbx\n"; }

    // Last instruction first, which is the order an unwinder undoes them in.
    unwindCodes_ = 0;
    unwindData_.clear();
    const std::string p = "\"$LNfp$" + fnName_ + "\"";
    const std::string al = "\"$LNalloc$" + fnName_ + "\"";
    const std::string pu = "\"$LNpush$" + fnName_ + "\"";
    // UWOP_SAVE_NONVOL is 4 with the register in the high nibble, and the
    // slot's offset up from rsp - which rbp equals - in eights.
    for (std::size_t i = saves_.size(); i-- > 0;) {
        const int id = opt::parseReg(saves_[i].reg).id;
        unwindData_ += "  .byte \"$LNsave" + std::to_string(i) + "$" + fnName_ + "\"-" + b + "\n";
        unwindData_ += "  .byte " + std::to_string((id << 4) | 4) + "\n  .short " +
                       std::to_string((frameSize + saves_[i].disp) / 8) + "\n";
        unwindCodes_ += 2;
    }
    // UWOP_SET_FPREG is 3; the frame offset is in the header and is zero
    // because rbp is set to rsp exactly.
    unwindData_ += "  .byte " + p + "-" + b + "\n  .byte 3\n";
    unwindCodes_ += 1;
    if (frameSize > 0) {
        unwindData_ += "  .byte " + al + "-" + b + "\n";
        if (frameSize <= 128 && frameSize % 8 == 0) {
            // UWOP_ALLOC_SMALL is 2 with (size/8 - 1) in the high nibble.
            unwindData_ += "  .byte " +
                std::to_string(((frameSize / 8 - 1) << 4) | 2) + "\n";
            unwindCodes_ += 1;
        } else {
            unwindData_ += "  .byte 1\n  .short " +
                std::to_string((frameSize + 7) / 8) + "\n";
            unwindCodes_ += 2;
        }
    }
    // UWOP_PUSH_NONVOL is 0 with the register in the high nibble - rbx is 3, rbp is 5.
    if (realign > 0) {
        unwindData_ += "  .byte \"$LNpushb$" + fnName_ + "\"-" + b + "\n  .byte 0x30\n";
        unwindCodes_ += 1;
    }
    unwindData_ += "  .byte " + pu + "-" + b + "\n  .byte 0x50\n";
    unwindCodes_ += 1;
}

void CoffSpelling::functionEnd(const std::string &name) {
    const std::string b = "\"$LNbeg$" + name + "\"";
    const std::string e = "\"$LNend$" + name + "\"";
    const std::string u = "\"$unwind$" + name + "\"";
    o_ += e + ":\n";

    // **Associative where the function is mergeable**, so the unwind data is
    // discarded with the copy it belongs to rather than surviving it.
    const std::string assoc =
        mergeable_ ? ",associative," + sym(name) : std::string();
    o_ += "  .section .xdata,\"dr\"" + assoc + "\n";
    o_ += "  .p2align 3\n";
    o_ += u + ":\n";
    // Version 1, and the flags in the top five bits. 0x19 is EHANDLER and
    // UHANDLER together, which is what a frame with a FuncInfo needs.
    o_ += std::string("  .byte ") + (hasEh_ ? "0x19" : "0x01") + "\n";
    o_ += "  .byte \"$LNprolog$" + name + "\"-" + b + "\n";
    o_ += "  .byte " + std::to_string(unwindCodes_) + "\n";
    o_ += "  .byte 0x05\n";
    o_ += unwindData_;
    // The codes are padded to an even count, and the handler goes after.
    if (unwindCodes_ % 2 != 0) o_ += "  .short 0\n";
    if (hasEh_) {
        o_ += "  .long __CxxFrameHandler3@IMGREL\n";
        o_ += "  .long \"$cppxdata$" + name + "\"@IMGREL\n";
    }
    o_ += "  .section .pdata,\"dr\"" + assoc + "\n";
    o_ += "  .p2align 2\n";
    o_ += "  .long " + b + "@IMGREL\n";
    o_ += "  .long " + e + "@IMGREL\n";
    o_ += "  .long " + u + "@IMGREL\n";
    o_ += "  .text\n";

    unwindData_.clear();
    unwindCodes_ = 0;
    hasEh_ = false;
    saves_.clear();
    mergeable_ = false;
}

// **The whole frame moved, so every offset into it moves with it.**
void CoffSpelling::op(const Op &x) {
    // The symbol's offset within the image's TLS template, cl's IMAGE_REL_AMD64_SECREL (measured
    // with dumpbin), off the thread's copy of the template in the base register.
    if (x.kind == Op::Tls) {
        o_ += sym(std::string(x.text.p, x.text.n)); o_ += "@SECREL32("; o_ += x.index; o_ += ')';
        return;
    }
    if (x.kind != Op::Mem || std::string(x.text.p, x.text.n) != "%rbp") {
        GnuSpelling::op(x);
        return;
    }
    long long d = (x.hasDisp ? x.disp : 0) + frameSize_;
    if (d != 0) appendNum(o_, d);
    o_ += '(';
    o_ += std::string(x.text.p, x.text.n);
    if (x.scale != 0) { o_ += ','; o_ += x.index; o_ += ','; appendNum(o_, x.scale); }
    o_ += ')';
}
