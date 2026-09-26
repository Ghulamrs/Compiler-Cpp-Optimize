#include "X86_64Linux.h"

#include "../Mangle.h"
#include "../Source.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ostream>
#include <sstream>

int LinuxX86_64Target::sizeOf(Kind k) const {
    switch (k) {
    case Kind::Void:                                   return 1;
    case Kind::Bool:                                       return 1;
    case Kind::Char: case Kind::SChar: case Kind::UChar:   return 1;
    case Kind::Short: case Kind::UShort:                   return 2;
    case Kind::WChar:                                      return sizeOf(wcharType());
    case Kind::Char16:                                     return 2;
    case Kind::Char32:                                     return 4;
    case Kind::Int: case Kind::UInt:                       return 4;
    case Kind::Long: case Kind::ULong:                     return 8;
    case Kind::LongLong: case Kind::ULongLong:             return 8;
    case Kind::Float:                                      return 4;
    case Kind::Double:                                     return 8;

    case Kind::LongDouble:                                 return 16;
    case Kind::Pointer: case Kind::NullPtr:                                    return 8;
    default:
        std::fprintf(stderr, "target: no size for this type yet\n");
        std::exit(1);
    }
}

int LinuxX86_64Target::alignOf(Kind k) const { return sizeOf(k); }

static void classifyInto(const Type *t, int base, std::vector<bool> &sse,
                         std::vector<bool> &reached, const Target &target) {
    if (t->isStructOrUnion()) {
        for (const Member &m : t->members())
            classifyInto(m.type, base + m.offset, sse, reached, target);
        return;
    }
    if (t->isArray()) {
        int step = t->pointee()->size(target);
        for (long long i = 0; i < t->length(); i++)
            classifyInto(t->pointee(), base + static_cast<int>(i) * step, sse,
                         reached, target);
        return;
    }

    int from = base / 8;
    int to = (base + t->size(target) - 1) / 8;
    for (int i = from; i <= to && i < static_cast<int>(sse.size()); i++) {
        reached[i] = true;
        if (!t->isFloating()) sse[i] = false;
    }
}

std::vector<bool> classifyEightbytes(const Type *t, const Target &target) {
    int size = t->size(target);
    const std::size_t lanes = static_cast<std::size_t>((size + 7) / 8);
    std::vector<bool> sse(lanes, true);
    std::vector<bool> reached(lanes, false);
    classifyInto(t, 0, sse, reached, target);

    // **A class with no data members takes no register at all.** SysV calls an
    // eightbyte nothing reaches NO_CLASS and clang bears it out; an empty lane
    // list says so. Before this it went in xmm0 and segfaulted on Linux alone.
    bool any = false;
    for (std::size_t i = 0; i < lanes; i++) if (reached[i]) any = true;
    if (!any) return std::vector<bool>();

    for (std::size_t i = 0; i < lanes; i++)
        if (!reached[i]) sse[i] = false;
    return sse;
}

static const char *const kArgRegs[] = { "%rdi", "%rsi", "%rdx", "%rcx", "%r8", "%r9" };
static const char *const kSseRegs[] = { "%xmm0", "%xmm1", "%xmm2", "%xmm3",
                                        "%xmm4", "%xmm5", "%xmm6", "%xmm7" };
static const char *const kPreserved[] = { "%rbx", "%rbp", "%rsp", "%r12", "%r13", "%r14", "%r15" };

// System V AMD64. Each field is set by name; what is left out keeps the default
// in Abi.h, which is the answer this ABI gives - positional false, no shadow
// space, no by-reference aggregates, no homogeneous float rule.
static Abi sysV() {
    Abi a;
    a.intRegs = kArgRegs;               a.intCount = 6;
    a.sseRegs = kSseRegs;               a.sseCount = 8;
    a.structReturnLimit = 16;
    a.variadicSseCountInAl = true;
    a.scratch = "%rdi";                 a.scratch32 = "%edi";
    a.preservedRegs = kPreserved;       a.preservedCount = 7;
    a.elfSymbolAttributes = true;
    return a;
}

static const Abi kSysVAbi = sysV();

const Abi &X86_64LinuxBackend::abi() const { return kSysVAbi; }

static const char *const kLinuxMacros[] = {
    "__x86_64__=1", "__x86_64=1", "__amd64__=1", "__amd64=1",
    "__linux__=1", "__linux=1", "__unix__=1", "__unix=1",
    "__ELF__=1", "__LP64__=1", "_LP64=1", nullptr,
};
const char *const *X86_64LinuxBackend::identityMacros() const { return kLinuxMacros; }

void X86_64Linux::push() {
    if (tempsInSlots()) {
        a_->ins("mov", reg("%rax"), tempSlot(tempDepth_++));
        if (tempDepth_ > tempHigh_) tempHigh_ = tempDepth_;
        return;
    }
    pushArg();
}
void X86_64Linux::pop(const char *into) {
    if (tempsInSlots()) { a_->ins("mov", tempSlot(--tempDepth_), reg(into)); return; }
    a_->ins("pop", reg(into));
    depth_--;
}
void X86_64Linux::pushArg() { a_->ins("push", reg("%rax")); depth_++; }

void X86_64Linux::pushF() {
    if (tempsInSlots()) {
        a_->ins("movsd", reg("%xmm0"), tempSlot(tempDepth_++));
        if (tempDepth_ > tempHigh_) tempHigh_ = tempDepth_;
        return;
    }
    pushFArg();
}
void X86_64Linux::popF(const char *into) {
    if (tempsInSlots()) { a_->ins("movsd", tempSlot(--tempDepth_), reg(into)); return; }
    a_->ins("movsd", mem("%rsp"), reg(into));
    a_->ins("add", immText("8"), reg("%rsp"));
    depth_--;
}
void X86_64Linux::pushFArg() {
    a_->ins("sub", immText("8"), reg("%rsp"));
    a_->ins("movsd", reg("%xmm0"), mem("%rsp"));
    depth_++;
}

void X86_64Linux::pushX87() {
    a_->ins("sub", immText("16"), reg("%rsp"));
    a_->ins("fstpt", mem("%rsp"));
    depth_ += 2;
}
void X86_64Linux::popX87() {
    a_->ins("fldt", mem("%rsp"));
    a_->ins("add", immText("16"), reg("%rsp"));
    depth_ -= 2;
}

Kind X86_64Linux::genKind(const Type *t) const {
    if (t->kind() == Kind::LongDouble && !isX87(t)) return Kind::Double;
    return t->kind();
}

const char *X86_64Linux::acc(const Type *t) const {
    return t->size(target_) == 8 ? "%rax" : "%eax";
}
const char *X86_64Linux::rhs(const Type *t) const {
    return t->size(target_) == 8 ? abi_.scratch : abi_.scratch32;
}

static bool msInRegister(int size) {
    return size == 1 || size == 2 || size == 4 || size == 8;
}

static const char *narrower(const char *reg64, int bytes) {
    struct Row { const char *r64, *r32, *r16, *r8; };
    static const Row rows[] = {
        { "%rax", "%eax",  "%ax",   "%al"   },
        { "%rdx", "%edx",  "%dx",   "%dl"   },
        { "%rcx", "%ecx",  "%cx",   "%cl"   },
        { "%r11", "%r11d", "%r11w", "%r11b" },
    };
    for (const Row &row : rows) {
        if (std::strcmp(reg64, row.r64) != 0) continue;
        return bytes >= 4 ? row.r32 : bytes >= 2 ? row.r16 : row.r8;
    }
    return reg64;
}

void X86_64Linux::storeTailFromReg(const char *reg64, long long off,
                                   const char *base, int left) {
    int done = 0, shifted = 0;
    auto bring = [&](int want) {
        if (want == shifted) return;
        a_->ins("shr", imm((want - shifted) * 8), reg(reg64));
        shifted = want;
    };
    if (left - done >= 4) {
        a_->ins("movl", reg(narrower(reg64, 4)), mem(off + done, base));
        done += 4;
    }
    if (left - done >= 2) {
        bring(done);
        a_->ins("movw", reg(narrower(reg64, 2)), mem(off + done, base));
        done += 2;
    }
    if (left - done >= 1) {
        bring(done);
        a_->ins("movb", reg(narrower(reg64, 1)), mem(off + done, base));
    }
}

void X86_64Linux::copyTailMem(long long from, long long to, int left) {
    int done = 0;
    if (left - done >= 4) {
        a_->ins("movl", mem(from + done, "%rbp"), reg("%eax"));
        a_->ins("movl", reg("%eax"), mem(to + done, "%rbp"));
        done += 4;
    }
    if (left - done >= 2) {
        a_->ins("movzwl", mem(from + done, "%rbp"), reg("%eax"));
        a_->ins("movw", reg("%ax"), mem(to + done, "%rbp"));
        done += 2;
    }
    if (left - done >= 1) {
        a_->ins("movzbl", mem(from + done, "%rbp"), reg("%eax"));
        a_->ins("movb", reg("%al"), mem(to + done, "%rbp"));
    }
}

// **Built with no scratch at all, one byte at a time from the top down.** The
// obvious way round needs a second register, a 32-bit write zeroing the upper
// half of its destination; going downwards an 8-bit write leaves the rest alone.
void X86_64Linux::loadTailToReg(const char *reg64, long long off,
                                const char *base, int left) {
    a_->ins("movzbl", mem(off + left - 1, base), reg(narrower(reg64, 4)));
    for (int i = left - 2; i >= 0; i--) {
        a_->ins("shl", imm(8), reg(reg64));
        a_->ins("orb", mem(off + i, base), reg(narrower(reg64, 1)));
    }
}

void X86_64Linux::msAggregateToRax(const Type *t, int slot) {
    int size = t->size(target_);
    if (msInRegister(size)) {
        if (size == 8)      a_->ins("mov", mem("%rax"), reg("%rax"));
        else if (size == 4) a_->ins("movl", mem("%rax"), reg("%eax"));
        else if (size == 2) a_->ins("movzwl", mem("%rax"), reg("%eax"));
        else                a_->ins("movzbl", mem("%rax"), reg("%eax"));
        return;
    }
    msCopyToSlot(t, slot, "%rax");
    a_->ins("lea", local(slot), reg("%rax"));
}

void X86_64Linux::msCopyToSlot(const Type *t, int slot, const char *from) {
    int size = t->size(target_);
    int off = 0;
    while (size - off >= 8) {
        a_->ins("mov", mem(off, from), reg("%r11"));
        a_->ins("mov", reg("%r11"), mem((off - slot), "%rbp"));
        off += 8;
    }
    while (size - off >= 4) {
        a_->ins("movl", mem(off, from), reg("%r11d"));
        a_->ins("movl", reg("%r11d"), mem((off - slot), "%rbp"));
        off += 4;
    }
    while (size - off >= 2) {
        a_->ins("movzwl", mem(off, from), reg("%r11d"));
        a_->ins("movw", reg("%r11w"), mem((off - slot), "%rbp"));
        off += 2;
    }
    while (size - off >= 1) {
        a_->ins("movzbl", mem(off, from), reg("%r11d"));
        a_->ins("movb", reg("%r11b"), mem((off - slot), "%rbp"));
        off += 1;
    }
}

void X86_64Linux::unsupported(const char *what) {
    std::fprintf(stderr, "codegen: %s is not supported yet by the %s backend\n",
                 what, target_.name());
    std::exit(1);
}

int X86_64Linux::takeSlot(bool sse, int &ints, int &sses) const {
    int taken = sse ? sses : ints;
    if (abi_.positional) { ints++; sses++; }
    else if (sse)        sses++;
    else                 ints++;
    return taken;
}

void X86_64Linux::canonicalise(const Type *t) {
    int sz = t->size(target_);
    bool sign = t->isSigned(target_);
    if (sz == 1) a_->ins(sign ? "movsbq" : "movzbq", reg("%al"), reg("%rax"));
    else if (sz == 2) a_->ins(sign ? "movswq" : "movzwq", reg("%ax"), reg("%rax"));
    else if (sz == 4) { if (sign) a_->ins("movslq", reg("%eax"), reg("%rax")); else a_->ins("mov", reg("%eax"), reg("%eax")); }
}

void X86_64Linux::genAddr(const Expr &e) {
    if (const Var *v = dynamic_cast<const Var *>(&e)) {
        if (v->isLocal()) a_->ins("lea", local(v->offset()), reg("%rax"));
        else              a_->ins("lea", rip(v->symbol()), reg("%rax"));
        return;
    }
    if (const Unary *u = dynamic_cast<const Unary *>(&e)) {
        if (u->op() == '*') { u->operand().accept(*this); return; }
    }
    if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(&e)) {
        if (m->isBitField()) {
            std::fprintf(stderr, "codegen: '%s' is a bit-field and has no address\n",
                         m->name().c_str());
            std::exit(1);
        }
        genAddr(m->object());
        if (m->offset() != 0) a_->ins("add", imm(m->offset()), reg("%rax"));
        return;
    }
    if (const StrLit *s = dynamic_cast<const StrLit *>(&e)) {
        a_->ins("lea", rip(s->label()), reg("%rax"));
        return;
    }
    if (const Call *c = dynamic_cast<const Call *>(&e)) {
        if (c->type()->isStructOrUnion()) { c->accept(*this); return; }
    }
    if (const Conditional *q = dynamic_cast<const Conditional *>(&e)) {
        if (q->type()->isStructOrUnion()) { q->accept(*this); return; }
    }
    // **A constructed temporary is a Comma, and it has an address.**
    if (const Comma *c = dynamic_cast<const Comma *>(&e)) {
        c->left().accept(*this);
        genAddr(c->right());
        return;
    }
    std::fprintf(stderr, "codegen: this has no address\n");
    std::exit(1);
}

void X86_64Linux::load(const Type *t) {
    if (t->isArray() || t->isStructOrUnion()) return;

    if (isX87(t))                    { a_->ins("fldt", mem("%rax")); return; }
    if (genKind(t) == Kind::Float)   { a_->ins("movss", mem("%rax"), reg("%xmm0")); return; }
    if (genKind(t) == Kind::Double)  { a_->ins("movsd", mem("%rax"), reg("%xmm0")); return; }

    int sz = t->size(target_);
    bool sign = t->isSigned(target_);
    if (sz == 1)      a_->ins(sign ? "movsbq" : "movzbq", mem("%rax"), reg("%rax"));
    else if (sz == 2) a_->ins(sign ? "movswq" : "movzwq", mem("%rax"), reg("%rax"));
    else if (sz == 4) if (sign) a_->ins("movslq", mem("%rax"), reg("%rax")); else a_->ins("movl", mem("%rax"), reg("%eax"));
    else              a_->ins("movq", mem("%rax"), reg("%rax"));
}

void X86_64Linux::store(const Type *t) {
    const char *at = abi_.scratch;
    if (isX87(t))                   { a_->ins("fstpt", mem(at)); return; }
    if (genKind(t) == Kind::Float)  { a_->ins("movss", reg("%xmm0"), mem(at)); return; }
    if (genKind(t) == Kind::Double) { a_->ins("movsd", reg("%xmm0"), mem(at)); return; }

    switch (t->size(target_)) {
    case 1: a_->ins("movb", reg("%al"), mem(at)); return;
    case 2: a_->ins("movw", reg("%ax"), mem(at)); return;
    case 4: a_->ins("movl", reg("%eax"), mem(at)); return;
    default: a_->ins("movq", reg("%rax"), mem(at)); return;
    }
}

void X86_64Linux::storeAt(const Type *t, int offset) {
    if (isX87(t))                   { a_->ins("fstpt", local(offset)); return; }
    if (genKind(t) == Kind::Float)  { a_->ins("movss", reg("%xmm0"), local(offset)); return; }
    if (genKind(t) == Kind::Double) { a_->ins("movsd", reg("%xmm0"), local(offset)); return; }
    switch (t->size(target_)) {
    case 1: a_->ins("movb", reg("%al"), local(offset)); return;
    case 2: a_->ins("movw", reg("%ax"), local(offset)); return;
    case 4: a_->ins("movl", reg("%eax"), local(offset)); return;
    default: a_->ins("movq", reg("%rax"), local(offset)); return;
    }
}

void X86_64Linux::loadX87Const(long double v) {
    unsigned long long lo = 0;
    unsigned int hi = 0;
    x87Parts(v, &lo, &hi);

    a_->ins("sub", immText("16"), reg("%rsp"));
    a_->ins("movabs", imm(lo), reg("%rax"));
    a_->ins("mov", reg("%rax"), mem("%rsp"));
    a_->ins("movw", imm(hi), mem(8, "%rsp"));
    a_->ins("fldt", mem("%rsp"));
    a_->ins("add", immText("16"), reg("%rsp"));
}

void X86_64Linux::visit(const Num &n) {
    if (!n.type()->isFloating()) {
        a_->ins("mov", imm(n.value()), reg("%rax"));
        return;
    }
    if (isX87(n.type())) { loadX87Const(n.dvalue()); return; }
    if (genKind(n.type()) == Kind::Float) {
        float f = static_cast<float>(n.dvalue());
        unsigned int bits;
        std::memcpy(&bits, &f, 4);
        a_->ins("mov", imm(bits), reg("%eax"));
        a_->ins("movd", reg("%eax"), reg("%xmm0"));
    } else {
        double d = n.dvalue();
        unsigned long long bits;
        std::memcpy(&bits, &d, 8);
        a_->ins("movabs", imm(bits), reg("%rax"));
        a_->ins("movq", reg("%rax"), reg("%xmm0"));
    }
}

void X86_64Linux::visit(const Var &n) {
    genAddr(n);
    load(n.type());
}

void X86_64Linux::visit(const StrLit &n) { genAddr(n); }

void X86_64Linux::visit(const MemberAccess &n) {
    if (n.isBitField()) {
        bitFieldUnitAddr(n);
        bitFieldExtract(n);
        return;
    }
    genAddr(n);
    load(n.type());
}

// From (%rax) to (scratch), which is left as it was; %rax and %rcx are not.
// **Where the levels part, as cl's /Os and /Ot do over a block move**: unrolled
// is nine bytes of code a word, `rep movsq` about fifteen for the whole block
// and slower to start, so the level that favours space takes it from three
// words up. rsi and rdi are the caller's on Windows, and pushed round it.
void X86_64Linux::copyBlock(int size) {
    const char *from = "%rax", *to = abi_.scratch;
    const int words = size / 8;
    const bool string = words >= 3 && optimizer_ && optimizer_->copiesByString();
    bool save = false;
    int off = 0;
    if (string) {
        for (int i = 0; i < abi_.preservedCount; ++i)
            if (std::strcmp(abi_.preservedRegs[i], "%rsi") == 0) save = true;
        if (save) { a_->ins("push", reg("%rsi")); a_->ins("push", reg("%rdi")); }
        a_->ins("mov", reg(from), reg("%rsi"));
        if (std::strcmp(to, "%rdi") != 0) a_->ins("mov", reg(to), reg("%rdi"));
        a_->ins("movl", imm(words), reg("%ecx"));
        a_->ins("rep movsq");
        // The tail goes from where the string instruction stopped.
        from = "%rsi"; to = "%rdi";
        size -= 8 * words;
    }
    while (size - off >= 8) {
        a_->ins("mov", mem(off, from), reg("%rcx"));
        a_->ins("mov", reg("%rcx"), mem(off, to));
        off += 8;
    }
    while (size - off >= 4) {
        a_->ins("movl", mem(off, from), reg("%ecx"));
        a_->ins("movl", reg("%ecx"), mem(off, to));
        off += 4;
    }
    while (size - off >= 2) {
        a_->ins("movw", mem(off, from), reg("%cx"));
        a_->ins("movw", reg("%cx"), mem(off, to));
        off += 2;
    }
    while (size - off >= 1) {
        a_->ins("movb", mem(off, from), reg("%cl"));
        a_->ins("movb", reg("%cl"), mem(off, to));
        off += 1;
    }
    if (save) { a_->ins("pop", reg("%rdi")); a_->ins("pop", reg("%rsi")); }
    else if (string && std::strcmp(abi_.scratch, "%rdi") == 0) a_->ins("sub", imm(8 * words), reg("%rdi"));
}

void X86_64Linux::bitFieldUnitAddr(const MemberAccess &m) {
    genAddr(m.object());
    if (m.offset() != 0) a_->ins("add", imm(m.offset()), reg("%rax"));
}

void X86_64Linux::bitFieldExtract(const MemberAccess &m) {
    load(m.type());
    int left = 64 - m.bitOffset() - m.width();
    int right = 64 - m.width();
    if (left > 0) a_->ins("shl", imm(left), reg("%rax"));
    a_->ins(m.type()->isSigned(target_) ? "sar" : "shr", imm(right), reg("%rax"));
}

void X86_64Linux::bitFieldInsert(const MemberAccess &m) {
    unsigned long long ones = (m.width() == 64) ? ~0ULL : ((1ULL << m.width()) - 1);
    unsigned long long mask = ones << m.bitOffset();

    a_->ins("mov", reg("%rax"), reg("%rdx"));
    a_->ins("movabs", imm(ones), reg("%rcx"));
    a_->ins("and", reg("%rcx"), reg("%rdx"));
    if (m.bitOffset() != 0) a_->ins("shl", imm(m.bitOffset()), reg("%rdx"));

    a_->ins("push", reg("%rax"));
    a_->ins("mov", reg(abi_.scratch), reg("%rax"));
    load(m.type());
    a_->ins("movabs", imm(~mask), reg("%rcx"));
    a_->ins("and", reg("%rcx"), reg("%rax"));
    a_->ins("or", reg("%rdx"), reg("%rax"));
    store(m.type());
    a_->ins("pop", reg("%rax"));

    int right = 64 - m.width();
    a_->ins("shl", imm(right), reg("%rax"));
    a_->ins(m.type()->isSigned(target_) ? "sar" : "shr", imm(right), reg("%rax"));
}

void X86_64Linux::visit(const Assign &n) {
    const MemberAccess *bf = dynamic_cast<const MemberAccess *>(&n.target());
    if (bf != nullptr && !bf->isBitField()) bf = nullptr;

    n.value().accept(*this);
    bool x87 = isX87(n.type());
    bool inSse = !x87 && n.type()->isFloating();
    if (x87)          pushX87();
    else if (inSse)   pushF();
    else              push();

    if (bf) bitFieldUnitAddr(*bf);
    else    genAddr(n.target());
    a_->ins("mov", reg("%rax"), reg(abi_.scratch));

    if (x87)        popX87();
    else if (inSse) popF("%xmm0");
    else            pop("%rax");

    if (n.type()->isStructOrUnion()) {
        copyBlock(n.type()->size(target_));
        a_->ins("mov", reg(abi_.scratch), reg("%rax"));
        return;
    }
    if (bf) { bitFieldInsert(*bf); return; }

    store(n.type());
    if (!n.type()->isFloating()) canonicalise(n.type());
}

void X86_64Linux::visit(const Postfix &n) {
    genAddr(n.target());
    push();
    load(n.type());

    if (isX87(n.type())) {
        a_->ins("fld", reg("%st(0)"));
        a_->ins("fld1");

        a_->ins(n.increment() ? "faddp" : "fsubrp", reg("%st"), reg("%st(1)"));
        pop(abi_.scratch);
        store(n.type());
        return;
    }

    if (n.type()->isFloating()) {
        pushF();
        bool single = genKind(n.type()) == Kind::Float;
        if (single) {
            float one = 1.0f;
            unsigned int bits;
            std::memcpy(&bits, &one, sizeof bits);
            a_->ins("mov", imm(bits), reg("%eax"));
            a_->ins("movd", reg("%eax"), reg("%xmm1"));
            a_->ins(n.increment() ? "addss" : "subss", reg("%xmm1"), reg("%xmm0"));
        } else {
            double one = 1.0;
            unsigned long long bits;
            std::memcpy(&bits, &one, sizeof bits);
            a_->ins("movabs", imm(bits), reg("%rax"));
            a_->ins("movq", reg("%rax"), reg("%xmm1"));
            a_->ins(n.increment() ? "addsd" : "subsd", reg("%xmm1"), reg("%xmm0"));
        }
        popF("%xmm1");
        pop(abi_.scratch);
        store(n.type());
        a_->ins("movapd", reg("%xmm1"), reg("%xmm0"));
        return;
    }

    push();
    a_->ins(n.increment() ? "add" : "sub", imm(n.step()), reg("%rax"));
    pop("%rdx");
    pop(abi_.scratch);
    store(n.type());
    a_->ins("mov", reg("%rdx"), reg("%rax"));
}

void X86_64Linux::visit(const Unary &n) {
    if (n.op() == '&') { genAddr(n.operand()); return; }
    if (n.op() == '*') {
        n.operand().accept(*this);
        load(n.type());
        return;
    }

    n.operand().accept(*this);
    if (n.op() == '-' && isX87(n.type())) {
        a_->ins("fchs");
        return;
    }
    if (n.op() == '!' && isX87(n.operand().type())) {
        a_->ins("fldz");
        a_->ins("fucomip", reg("%st(1)"), reg("%st"));
        a_->ins("fstp", reg("%st(0)"));
        a_->ins("sete", reg("%al"));
        a_->ins("setnp", reg("%cl"));
        a_->ins("and", reg("%cl"), reg("%al"));
        a_->ins("movzbq", reg("%al"), reg("%rax"));
        return;
    }
    if (n.op() == '-' && n.type()->isFloating()) {

        if (genKind(n.type()) == Kind::Double) {
            a_->ins("movabs", imm(0x8000000000000000ULL), reg("%rax"));
            a_->ins("movq", reg("%rax"), reg("%xmm1"));
            a_->ins("xorpd", reg("%xmm1"), reg("%xmm0"));
        } else {
            a_->ins("mov", imm(0x80000000U), reg("%eax"));
            a_->ins("movd", reg("%eax"), reg("%xmm1"));
            a_->ins("xorps", reg("%xmm1"), reg("%xmm0"));
        }
    } else if (n.op() == '-') {
        a_->ins("neg", reg(acc(n.type())));
        canonicalise(n.type());
    } else if (n.op() == '!' && n.operand().type()->isFloating()) {
        bool isDouble = genKind(n.operand().type()) == Kind::Double;
        a_->ins("pxor", reg("%xmm1"), reg("%xmm1"));
        a_->ins(isDouble ? "ucomisd" : "ucomiss", reg("%xmm1"), reg("%xmm0"));

        a_->ins("sete", reg("%al"));
        a_->ins("setnp", reg("%cl"));
        a_->ins("and", reg("%cl"), reg("%al"));
        a_->ins("movzbq", reg("%al"), reg("%rax"));
    } else if (n.op() == '!') {
        a_->ins("cmp", immText("0"), reg("%rax"));
        a_->ins("sete", reg("%al"));
        a_->ins("movzbq", reg("%al"), reg("%rax"));
    }
}

void X86_64Linux::x87ToInt(const Type *to) {
    a_->ins("sub", immText("16"), reg("%rsp"));
    a_->ins("fnstcw", mem(12, "%rsp"));
    a_->ins("movzwl", mem(12, "%rsp"), reg("%eax"));
    a_->ins("or", immText("0x0c00"), reg("%eax"));
    a_->ins("mov", reg("%ax"), mem(10, "%rsp"));
    a_->ins("fldcw", mem(10, "%rsp"));
    a_->ins("fistpq", mem("%rsp"));
    a_->ins("fldcw", mem(12, "%rsp"));
    a_->ins("mov", mem("%rsp"), reg("%rax"));
    a_->ins("add", immText("16"), reg("%rsp"));
    canonicalise(to);
}

void X86_64Linux::intToX87(const Type *from) {
    bool wideUnsigned = from->size(target_) == 8 && !from->isSigned(target_);
    a_->ins("sub", immText("16"), reg("%rsp"));
    a_->ins("mov", reg("%rax"), mem("%rsp"));
    a_->ins("fildq", mem("%rsp"));
    if (wideUnsigned) {
        int id = nextLabel();

        a_->ins("cmp", immText("0"), reg("%rax"));
        a_->ins("jns", lbl(label("nofix", id)));
        a_->ins("movabs", imm(0x8000000000000000ULL), reg("%rax"));
        a_->ins("mov", reg("%rax"), mem("%rsp"));
        a_->ins("movw", imm((16383 + 64)), mem(8, "%rsp"));
        a_->ins("fldt", mem("%rsp"));
        a_->ins("faddp", reg("%st"), reg("%st(1)"));
        a_->defLabel(label("nofix", id));
    }
    a_->ins("add", immText("16"), reg("%rsp"));
}

void X86_64Linux::genConversion(const Type *from, const Type *to) {
    if (to->isVoid()) {

        if (isX87(from)) a_->ins("fstp", reg("%st(0)"));
        return;
    }

    bool fromF = from->isFloating(), toF = to->isFloating();

    if (!fromF && !toF) { canonicalise(to); return; }

    if (isX87(to) && !isX87(from)) {
        if (!fromF) { intToX87(from); return; }
        a_->ins("sub", immText("16"), reg("%rsp"));
        if (genKind(from) == Kind::Float) {
            a_->ins("movss", reg("%xmm0"), mem("%rsp"));
            a_->ins("flds", mem("%rsp"));
        } else {
            a_->ins("movsd", reg("%xmm0"), mem("%rsp"));
            a_->ins("fldl", mem("%rsp"));
        }
        a_->ins("add", immText("16"), reg("%rsp"));
        return;
    }
    if (isX87(from) && !isX87(to)) {
        if (!toF) { x87ToInt(to); return; }
        a_->ins("sub", immText("16"), reg("%rsp"));
        if (genKind(to) == Kind::Float) {
            a_->ins("fstps", mem("%rsp"));
            a_->ins("movss", mem("%rsp"), reg("%xmm0"));
        } else {
            a_->ins("fstpl", mem("%rsp"));
            a_->ins("movsd", mem("%rsp"), reg("%xmm0"));
        }
        a_->ins("add", immText("16"), reg("%rsp"));
        return;
    }
    if (isX87(from) && isX87(to)) return;

    if (fromF && toF) {
        if (genKind(from) == genKind(to)) return;
        if (genKind(to) == Kind::Double) a_->ins("cvtss2sd", reg("%xmm0"), reg("%xmm0"));
        else                             a_->ins("cvtsd2ss", reg("%xmm0"), reg("%xmm0"));
        return;
    }

    if (!fromF && toF) {
        const char *op = genKind(to) == Kind::Double ? "cvtsi2sdq" : "cvtsi2ssq";

        if (from->size(target_) == 8 && !from->isSigned(target_)) {
            int id = nextLabel();
            a_->ins("cmp", imm(0), reg("%rax"));
            a_->ins("jns", lbl(label("uns", id)));
            a_->ins("mov", reg("%rax"), reg("%rdx"));
            a_->ins("shr", imm(1), reg("%rdx"));
            a_->ins("and", imm(1), reg("%eax"));
            a_->ins("or", reg("%rax"), reg("%rdx"));
            a_->ins(op, reg("%rdx"), reg("%xmm0"));
            a_->ins(genKind(to) == Kind::Double ? "addsd" : "addss",
                    reg("%xmm0"), reg("%xmm0"));
            a_->ins("jmp", lbl(label("unsend", id)));
            a_->defLabel(label("uns", id));
            a_->ins(op, reg("%rax"), reg("%xmm0"));
            a_->defLabel(label("unsend", id));
            return;
        }
        a_->ins(op, reg("%rax"), reg("%xmm0"));
        return;
    }

    const char *op = genKind(from) == Kind::Double ? "cvttsd2si" : "cvttss2si";

    // **`cvttsd2si` is a *signed* conversion, and there is no unsigned one.** A
    // `double` at or above 2^63 returns the integer indefinite value, so subtract
    // 2^63 first, convert what is left and put the bit back. arm64 has `fcvtzu`.
    if (to->size(target_) == 8 && !to->isSigned(target_)) {
        const bool dbl = genKind(from) == Kind::Double;
        const int id = nextLabel();
        if (dbl) {
            a_->ins("movabs", imm(0x43E0000000000000ULL), reg("%rdx"));
            a_->ins("movq", reg("%rdx"), reg("%xmm1"));       // 2^63 as double
        } else {
            a_->ins("mov", imm(0x5F000000ULL), reg("%edx"));
            a_->ins("movd", reg("%edx"), reg("%xmm1"));       // 2^63 as float
        }
        a_->ins(dbl ? "ucomisd" : "ucomiss", reg("%xmm1"), reg("%xmm0"));
        a_->ins("jae", lbl(label("u64big", id)));
        a_->ins(op, reg("%xmm0"), reg("%rax"));
        a_->ins("jmp", lbl(label("u64end", id)));
        a_->defLabel(label("u64big", id));
        a_->ins(dbl ? "subsd" : "subss", reg("%xmm1"), reg("%xmm0"));
        a_->ins(op, reg("%xmm0"), reg("%rax"));
        a_->ins("movabs", imm(0x8000000000000000ULL), reg("%rdx"));
        a_->ins("xor", reg("%rdx"), reg("%rax"));
        a_->defLabel(label("u64end", id));
        return;
    }

    a_->ins(op, reg("%xmm0"), reg("%rax"));
    canonicalise(to);
}

void X86_64Linux::visit(const Cast &n) {
    n.value().accept(*this);
    genConversion(n.value().type(), n.type());
}

void X86_64Linux::genX87Binary(const Binary &n) {
    n.lhs().accept(*this);
    pushX87();
    n.rhs().accept(*this);
    a_->ins("fldt", mem("%rsp"));
    a_->ins("add", immText("16"), reg("%rsp"));
    depth_ -= 2;

    switch (n.op()) {
    case BinOp::Add: a_->ins("faddp", reg("%st"), reg("%st(1)")); return;
    case BinOp::Sub: a_->ins("fsubp", reg("%st"), reg("%st(1)")); return;
    case BinOp::Mul: a_->ins("fmulp", reg("%st"), reg("%st(1)")); return;
    case BinOp::Div: a_->ins("fdivp", reg("%st"), reg("%st(1)")); return;
    default: break;
    }

    const char *set = nullptr;
    bool swapped = false;
    switch (n.op()) {
    case BinOp::Eq: case BinOp::Ne: break;
    case BinOp::Lt: set = "seta";  swapped = true; break;
    case BinOp::Le: set = "setae"; swapped = true; break;
    case BinOp::Gt: set = "seta";  break;
    case BinOp::Ge: set = "setae"; break;
    default:
        std::fprintf(stderr, "codegen: that operator has no floating form\n");
        std::exit(1);
    }

    if (swapped) a_->ins("fxch", reg("%st(1)"));
    a_->ins("fucomip", reg("%st(1)"), reg("%st"));
    a_->ins("fstp", reg("%st(0)"));

    if (n.op() == BinOp::Eq) {
        a_->ins("sete", reg("%al"));
        a_->ins("setnp", reg("%cl"));
        a_->ins("and", reg("%cl"), reg("%al"));
    } else if (n.op() == BinOp::Ne) {
        a_->ins("setne", reg("%al"));
        a_->ins("setp", reg("%cl"));
        a_->ins("or", reg("%cl"), reg("%al"));
    } else {
        a_->ins(set, reg("%al"));
    }
    a_->ins("movzbq", reg("%al"), reg("%rax"));
}

void X86_64Linux::genFloatBinary(const Binary &n) {
    const Type *t = n.lhs().type();
    if (isX87(t)) { genX87Binary(n); return; }
    bool isDouble = genKind(t) == Kind::Double;
    const char *sfx = isDouble ? "sd" : "ss";

    n.rhs().accept(*this);
    pushF();
    n.lhs().accept(*this);
    popF("%xmm1");

    switch (n.op()) {
    case BinOp::Add: a_->ins(std::string("add") + sfx, reg("%xmm1"), reg("%xmm0")); return;
    case BinOp::Sub: a_->ins(std::string("sub") + sfx, reg("%xmm1"), reg("%xmm0")); return;
    case BinOp::Mul: a_->ins(std::string("mul") + sfx, reg("%xmm1"), reg("%xmm0")); return;
    case BinOp::Div: a_->ins(std::string("div") + sfx, reg("%xmm1"), reg("%xmm0")); return;
    default: break;
    }

    const char *set = nullptr;
    bool swapped = false;
    switch (n.op()) {
    case BinOp::Eq: case BinOp::Ne: break;
    case BinOp::Lt: set = "seta";  swapped = true; break;
    case BinOp::Le: set = "setae"; swapped = true; break;
    case BinOp::Gt: set = "seta";  break;
    case BinOp::Ge: set = "setae"; break;
    default:
        std::fprintf(stderr, "codegen: that operator has no floating form\n");
        std::exit(1);
    }

    if (swapped) a_->ins(std::string("ucomi") + sfx, reg("%xmm0"), reg("%xmm1"));
    else         a_->ins(std::string("ucomi") + sfx, reg("%xmm1"), reg("%xmm0"));

    if (n.op() == BinOp::Eq) {
        a_->ins("sete", reg("%al"));
        a_->ins("setnp", reg("%cl"));
        a_->ins("and", reg("%cl"), reg("%al"));
    } else if (n.op() == BinOp::Ne) {
        a_->ins("setne", reg("%al"));
        a_->ins("setp", reg("%cl"));
        a_->ins("or", reg("%cl"), reg("%al"));
    } else {
        a_->ins(set, reg("%al"));
    }
    a_->ins("movzbq", reg("%al"), reg("%rax"));
}

void X86_64Linux::visit(const Binary &n) {
    if (n.op() == BinOp::LAnd || n.op() == BinOp::LOr) {
        int id = nextLabel();
        bool isAnd = n.op() == BinOp::LAnd;
        const char *shortJump = isAnd ? "je" : "jne";

        genTruth(n.lhs());
        a_->ins("cmp", immText("0"), reg("%rax"));
        a_->ins(shortJump, lbl(label("sc", id)));
        genTruth(n.rhs());
        a_->ins("cmp", immText("0"), reg("%rax"));
        a_->ins(shortJump, lbl(label("sc", id)));
        a_->ins("mov", imm((isAnd ? 1 : 0)), reg("%rax"));
        a_->ins("jmp", lbl(label("scend", id)));
        a_->defLabel(label("sc", id));
        a_->ins("mov", imm((isAnd ? 0 : 1)), reg("%rax"));
        a_->defLabel(label("scend", id));
        // Named by these jumps and nothing else, so droppable once threaded past.
        if (optimizer_) { optimizer_->jumpOnly(label("sc", id)); optimizer_->jumpOnly(label("scend", id)); }
        return;
    }

    if (n.lhs().type()->isFloating()) { genFloatBinary(n); return; }

    n.rhs().accept(*this);
    push();
    n.lhs().accept(*this);
    pop(abi_.scratch);

    const Type *t = n.lhs().type();
    const char *a = acc(t);
    const char *d = rhs(t);
    bool sign = t->isSigned(target_);
    bool wide = t->size(target_) == 8;

    switch (n.op()) {
    case BinOp::Add: a_->ins("add", reg(d), reg(a)); canonicalise(n.type()); return;
    case BinOp::Sub: a_->ins("sub", reg(d), reg(a)); canonicalise(n.type()); return;
    case BinOp::Mul: a_->ins("imul", reg(d), reg(a)); canonicalise(n.type()); return;
    case BinOp::BitAnd: a_->ins("and", reg(d), reg(a)); canonicalise(n.type()); return;
    case BinOp::BitOr:  a_->ins("or", reg(d), reg(a)); canonicalise(n.type()); return;
    case BinOp::BitXor: a_->ins("xor", reg(d), reg(a)); canonicalise(n.type()); return;

    case BinOp::Div:
    case BinOp::Mod:
        if (sign) { if (wide) a_->ins("cqo"); else a_->ins("cdq"); }
        else      a_->ins("xor", reg("%edx"), reg("%edx"));
        a_->ins(sign ? "idiv" : "div", reg(d));
        if (n.op() == BinOp::Mod) { if (wide) a_->ins("mov", reg("%rdx"), reg("%rax")); else a_->ins("mov", reg("%edx"), reg("%eax")); }
        canonicalise(n.type());
        return;

    case BinOp::Shl:
    case BinOp::Shr:
        a_->ins("mov", reg(abi_.scratch), reg("%rcx"));
        if (n.op() == BinOp::Shl) a_->ins("shl", reg("%cl"), reg(a));
        else                      a_->ins(sign ? "sar" : "shr", reg("%cl"), reg(a));
        canonicalise(n.type());
        return;

    default:
        break;
    }

    const char *set = nullptr;
    switch (n.op()) {
    case BinOp::Eq: set = "sete";  break;
    case BinOp::Ne: set = "setne"; break;
    case BinOp::Lt: set = sign ? "setl"  : "setb"; break;
    case BinOp::Le: set = sign ? "setle" : "setbe"; break;
    case BinOp::Gt: set = sign ? "setg"  : "seta"; break;
    case BinOp::Ge: set = sign ? "setge" : "setae"; break;
    default:
        std::fprintf(stderr, "codegen: unhandled binary operator\n");
        std::exit(1);
    }
    a_->ins("cmp", reg(d), reg(a));
    a_->ins(set, reg("%al"));
    a_->ins("movzbq", reg("%al"), reg("%rax"));
}

// **One walk of the parameter list, for the caller and the callee alike.** Both
// used to make this decision by hand, in two loops that had to stay in step; the
// hidden-return-pointer bug A-01 was them agreeing wrongly on both sides at once.
X86_64Linux::Placement X86_64Linux::placeArguments(
        const std::vector<const Type *> &types, bool hasThis, bool sret) const {
    // Microsoft puts `this` before the hidden pointer and Itanium puts the
    // pointer before everything - so who has claimed a register by the time the
    // first written argument is placed is the whole of the difference.
    const bool msThisFirst = sret && abi_.positional && hasThis;
    int ints = (sret && !msThisFirst) ? 1 : 0;
    int sses = (sret && abi_.positional && !msThisFirst) ? 1 : 0;
    int words = 0;

    Placement out;
    out.args.reserve(types.size());
    for (std::size_t i = 0; i < types.size(); i++) {
        // The pointer takes the slot after `this`, so everything from the second
        // argument on starts one further along.
        if (msThisFirst && i == 1) { ints++; sses++; }
        const Type *t = types[i];
        ArgPlace p;

        if (abi_.aggregatesByReference && t->isStructOrUnion()) {
            p.inMemory = ints + 1 > abi_.intCount;
            if (p.inMemory) {
                p.stackOffset = words * 8;
                p.stackWords = 1;
                words += 1;
            } else {
                p.lanes.push_back(false);
                p.regs.push_back(takeSlot(false, ints, sses));
            }
            out.args.push_back(p);
            continue;
        }

        bool memory = isX87(t) || containsX87(t, target_) ||
                      (t->isStructOrUnion() &&
                       t->size(target_) > abi_.structReturnLimit);
        if (!memory) {
            if (t->isStructOrUnion()) p.lanes = classifyEightbytes(t, target_);
            else                      p.lanes.push_back(t->isFloating());
            int wantInt = 0, wantSse = 0;
            for (bool sse : p.lanes) { if (sse) wantSse++; else wantInt++; }
            memory = ints + wantInt > abi_.intCount ||
                     sses + wantSse > abi_.sseCount;
        }

        if (memory) {
            p.lanes.clear();
            p.inMemory = true;
            int size = (t->isStructOrUnion() || isX87(t)) ? t->size(target_) : 8;
            // An argument aligned to 16 starts on an even word, which the caller
            // buys with a pad underneath and the callee reads as an aligned
            // offset - the same rule, said from the two ends.
            if (t->align(target_) >= 16 && words % 2 != 0) {
                p.padBelow = true;
                words += 1;
            }
            p.stackOffset = words * 8;
            p.stackWords = (size + 7) / 8;
            words += p.stackWords;
        } else {
            for (bool sse : p.lanes) p.regs.push_back(takeSlot(sse, ints, sses));
        }
        out.args.push_back(p);
    }
    // With `this` alone the loop never reached the pointer's slot; it is still taken.
    if (msThisFirst && types.size() < 2) { ints++; sses++; }
    out.intsUsed = ints;
    out.ssesUsed = sses;
    out.stackWords = words;
    return out;
}

// The Microsoft size test serves free functions only: a class returned from a
// member function travels through the hidden pointer whatever its size, so
// `hasThis` decides first. The parser chose the same giving the sret slot.
bool X86_64Linux::returnsThroughPointer(const Call &n) const {
    const bool byRef = abi_.aggregatesByReference;
    return n.type()->isStructOrUnion() &&
           (n.type()->nonTrivialCopy() || n.type()->hasDestructor() ||
            containsX87(n.type(), target_) ||
            (byRef ? (n.hasThis() ||
                      !msInRegister(n.type()->size(target_)))
                   : n.type()->size(target_) > abi_.structReturnLimit));
}

X86_64Linux::Placement X86_64Linux::placeCall(const Call &n) const {
    std::vector<const Type *> types;
    types.reserve(n.args().size());
    for (const ExprPtr &arg : n.args()) types.push_back(arg->type());
    return placeArguments(types, n.hasThis(), returnsThroughPointer(n));
}

void X86_64Linux::visit(const Call &n) {
    bool byRef = abi_.aggregatesByReference;
    bool sret = returnsThroughPointer(n);
    const bool msThisFirst = sret && abi_.positional && n.hasThis();

    const Placement plan = placeCall(n);
    const std::vector<ArgPlace> &place = plan.args;
    const int sses = plan.ssesUsed;
    const int stackSlots = plan.stackWords;

    // **At the floor, the call needs nothing of its own**: the prologue's
    // outgoing area is at rsp and rsp is aligned, and a stack argument is
    // stored into it where the callee will read it. That last holds only if
    // nothing evaluated after the store makes a call there that writes the
    // same words - the arguments are evaluated from the last stack one down,
    // then the callee, then the register ones, so only the first of those
    // may. Anywhere else - a value pushed above, a funclet's 32 bytes, a
    // later argument that may call - it allocates as it always has.
    bool atFloor = outgoing_ > 0 && floorDepth_ + depth_ == 0;
    if (atFloor && stackSlots > 0) {
        atFloor = !inFunclet_ && outgoing_ >= abi_.shadowBytes + 8 * stackSlots;
        bool first = true;
        for (std::size_t i = n.args().size(); atFloor && i-- > 0; ) {
            if (!place[i].inMemory) continue;
            if (!first && mayWriteArea(*n.args()[i])) atFloor = false;
            first = false;
        }
        if (atFloor && n.callee() != nullptr && mayWriteArea(*n.callee())) atFloor = false;
        for (std::size_t i = 0; atFloor && i < n.args().size(); i++)
            if (!place[i].inMemory && mayWriteArea(*n.args()[i])) atFloor = false;
    }
    int shadowSlots = atFloor ? 0 : abi_.shadowBytes / 8;
    const int pushedSlots = atFloor ? 0 : stackSlots;

    int padSlots = ((depth_ + pushedSlots + shadowSlots) % 2 != 0) ? 1 : 0;
    if (padSlots) { a_->ins("sub", immText("8"), reg("%rsp")); depth_++; }

    for (std::size_t i = n.args().size(); i-- > 0; ) {
        if (!place[i].inMemory) continue;
        const Type *t = n.args()[i]->type();
        n.args()[i]->accept(*this);
        if (atFloor) {
            // Only a word reaches here: an aggregate travels by reference on
            // the one ABI with an outgoing area, and there is no x87 value.
            const Op at = mem(abi_.shadowBytes + place[i].stackOffset, "%rsp");
            if (byRef && t->isStructOrUnion()) msAggregateToRax(t, n.argSlot(i));
            if (!t->isStructOrUnion() && t->isFloating()) a_->ins("movsd", reg("%xmm0"), at);
            else a_->ins("mov", reg("%rax"), at);
            continue;
        }
        if (byRef && t->isStructOrUnion()) {
            msAggregateToRax(t, n.argSlot(i));
            pushArg();
            if (place[i].padBelow) { a_->ins("sub", immText("8"), reg("%rsp")); depth_++; }
            continue;
        }
        if (!t->isStructOrUnion()) {
            if (isX87(t))             pushX87();
            else if (t->isFloating()) pushFArg();
            else                      pushArg();
            if (place[i].padBelow) { a_->ins("sub", immText("8"), reg("%rsp")); depth_++; }
            continue;
        }
        int size = t->size(target_);
        int slots = (size + 7) / 8;
        a_->ins("mov", reg("%rax"), reg("%rcx"));
        for (int k = slots; k-- > 0; ) {
            int off = k * 8;
            int left = size - off;
            if (left >= 8) {
                a_->ins("mov", mem(off, "%rcx"), reg("%rax"));
                pushArg();
                continue;
            }
            // **A partial lane is pushed as a zeroed word and then filled.** %rcx
            // holds the source and the loop still needs it, and reading eight
            // bytes would read past the object; the live bytes need neither.
            a_->ins("push", immText("0"));
            depth_++;
            int done = 0;
            if (left - done >= 4) {
                a_->ins("movl", mem(off + done, "%rcx"), reg("%eax"));
                a_->ins("movl", reg("%eax"), mem(done, "%rsp"));
                done += 4;
            }
            if (left - done >= 2) {
                a_->ins("movzwl", mem(off + done, "%rcx"), reg("%eax"));
                a_->ins("movw", reg("%ax"), mem(done, "%rsp"));
                done += 2;
            }
            if (left - done >= 1) {
                a_->ins("movzbl", mem(off + done, "%rcx"), reg("%eax"));
                a_->ins("movb", reg("%al"), mem(done, "%rsp"));
            }
        }
        if (place[i].padBelow) { a_->ins("sub", immText("8"), reg("%rsp")); depth_++; }
    }

    if (n.callee() != nullptr) {
        n.callee()->accept(*this);
        push();
    }

    for (const ExprPtr &arg : n.args()) {
        if (place[static_cast<std::size_t>(&arg - &n.args()[0])].inMemory) continue;
        arg->accept(*this);
        if (arg->type()->isFloating()) pushF(); else push();
    }
    for (std::size_t i = n.args().size(); i-- > 0; ) {
        if (place[i].inMemory) continue;
        const Type *t = n.args()[i]->type();
        if (!t->isStructOrUnion()) {
            if (place[i].lanes[0]) {

                if (abi_.positional && n.isVariadic() &&
                    static_cast<int>(i) >= n.namedArgs())
                    a_->ins("mov", tempsInSlots() ? tempSlot(tempDepth_ - 1) : mem("%rsp"),
                            reg(abi_.intRegs[place[i].regs[0]]));
                popF(abi_.sseRegs[place[i].regs[0]]);
            } else {
                pop(abi_.intRegs[place[i].regs[0]]);
            }
            continue;
        }
        pop("%rax");
        if (byRef) {
            msAggregateToRax(t, n.argSlot(i));
            a_->ins("mov", reg("%rax"), reg(abi_.intRegs[place[i].regs[0]]));
            continue;
        }
        int size = t->size(target_);
        for (std::size_t k = 0; k < place[i].lanes.size(); k++) {
            int off = static_cast<int>(k) * 8;
            int left = size - off;
            if (place[i].lanes[k]) {
                a_->ins(left >= 8 ? "movsd" : "movss", mem(off, "%rax"), reg(abi_.sseRegs[place[i].regs[k]]));
            } else if (left >= 8) {
                a_->ins("mov", mem(off, "%rax"), reg(abi_.intRegs[place[i].regs[k]]));
            } else {

                // The sixth place a partial lane is moved, and the one an audit
                // of the other five missed: an aggregate small enough to travel
                // in registers, %rax its address and %r11 the accumulator.
                loadTailToReg("%r11", off, "%rax", left);
                a_->ins("mov", reg("%r11"), reg(abi_.intRegs[place[i].regs[k]]));
            }
        }
    }

    if (n.callee() != nullptr) pop("%r11");

    if (sret)
        a_->ins("lea", mem((-n.resultSlot()), "%rbp"),
                reg(abi_.intRegs[msThisFirst ? 1 : 0]));

    if (const Function *body = inlineTarget(n, stackSlots)) {
        walkInPlace(*body);
        if (padSlots > 0) {
            a_->ins("add", imm(padSlots * 8), reg("%rsp"));
            depth_ -= padSlots;
        }
    } else {
        a_->ins("mov", imm(((n.isVariadic() && abi_.variadicSseCountInAl) ? sses : 0)), reg("%rax"));

        if (shadowSlots > 0) {
            a_->ins("sub", imm(abi_.shadowBytes), reg("%rsp"));
            depth_ += shadowSlots;
        }

        // **The call reads the registers the plan filled and no other**: the
        // optimizer need not keep the rest alive into it, nor copy into them.
        if (optimizer_) {
            opt::RegSet reads = (n.isVariadic() && abi_.variadicSseCountInAl) ? opt::bit(opt::RAX) : 0;
            for (int k = 0; k < plan.intsUsed && k < abi_.intCount; ++k) reads |= opt::bit(opt::parseReg(abi_.intRegs[k]).id);
            for (int k = 0; k < plan.ssesUsed && k < abi_.sseCount; ++k) reads |= opt::bit(opt::parseReg(abi_.sseRegs[k]).id);
            optimizer_->callArguments(reads);
        }
        if (n.callee() != nullptr) a_->ins("call", ind("%r11"));
        else                       a_->ins("call", lbl(n.symbol()));

        int unwind = pushedSlots + padSlots + shadowSlots;
        if (unwind > 0) {
            a_->ins("add", imm(unwind * 8), reg("%rsp"));
            depth_ -= unwind;
        }
    }

    if (sret) {
        a_->ins("lea", mem((-n.resultSlot()), "%rbp"), reg("%rax"));
        return;
    }

    if (byRef && n.type()->isStructOrUnion()) {
        int size = n.type()->size(target_);
        int to = -n.resultSlot();
        if (size == 8)      a_->ins("mov", reg("%rax"), mem(to, "%rbp"));
        else if (size == 4) a_->ins("movl", reg("%eax"), mem(to, "%rbp"));
        else if (size == 2) a_->ins("movw", reg("%ax"), mem(to, "%rbp"));
        else                a_->ins("movb", reg("%al"), mem(to, "%rbp"));
        a_->ins("lea", mem(to, "%rbp"), reg("%rax"));
        return;
    }

    if (n.type()->isStructOrUnion()) {
        std::vector<bool> lanes = classifyEightbytes(n.type(), target_);
        int size = n.type()->size(target_);
        int base = n.resultSlot();
        const char *ret[2] = { "%rax", "%rdx" };
        const char *sret[2] = { "%xmm0", "%xmm1" };
        int nextInt = 0, nextSse = 0;
        for (std::size_t k = 0; k < lanes.size(); k++) {
            int off = static_cast<int>(k) * 8 - base;
            int left = size - static_cast<int>(k) * 8;
            if (lanes[k]) {
                a_->ins(left >= 8 ? "movsd" : "movss", reg(sret[nextSse++]), mem(off, "%rbp"));
            } else {
                const char *r = ret[nextInt++];
                if (left >= 8) a_->ins("mov", reg(r), mem(off, "%rbp"));
                else           storeTailFromReg(r, off, "%rbp", left);
            }
        }
        a_->ins("lea", mem((-base), "%rbp"), reg("%rax"));
        return;
    }

    if (!n.type()->isVoid() && !n.type()->isFloating()) canonicalise(n.type());
}

void X86_64Linux::genTruth(const Expr &e) {
    e.accept(*this);
    if (isX87(e.type())) {
        a_->ins("fldz");
        a_->ins("fucomip", reg("%st(1)"), reg("%st"));
        a_->ins("fstp", reg("%st(0)"));
        a_->ins("setne", reg("%al"));
        a_->ins("setp", reg("%cl"));
        a_->ins("or", reg("%cl"), reg("%al"));
        a_->ins("movzbq", reg("%al"), reg("%rax"));
        return;
    }
    if (!e.type()->isFloating()) return;
    bool isDouble = genKind(e.type()) == Kind::Double;
    a_->ins("pxor", reg("%xmm1"), reg("%xmm1"));
    a_->ins(isDouble ? "ucomisd" : "ucomiss", reg("%xmm1"), reg("%xmm0"));

    a_->ins("setne", reg("%al"));
    a_->ins("setp", reg("%cl"));
    a_->ins("or", reg("%cl"), reg("%al"));
    a_->ins("movzbq", reg("%al"), reg("%rax"));
}

void X86_64Linux::visit(const VaArg &n) {
    n.list().accept(*this);
    const Type *t = n.type();

    if (abi_.positional) {
        a_->ins("mov", mem("%rax"), reg("%rcx"));
        a_->ins("lea", mem(8, "%rcx"), reg("%rdx"));
        a_->ins("mov", reg("%rdx"), mem("%rax"));
        a_->ins("mov", reg("%rcx"), reg("%rax"));
        load(t);
        return;
    }

    if (isX87(t)) {
        a_->ins("mov", mem(8, "%rax"), reg("%rdx"));
        a_->ins("add", immText("15"), reg("%rdx"));
        a_->ins("and", immText("-16"), reg("%rdx"));
        a_->ins("lea", mem(16, "%rdx"), reg("%rcx"));
        a_->ins("mov", reg("%rcx"), mem(8, "%rax"));
        a_->ins("mov", reg("%rdx"), reg("%rax"));
        load(t);
        return;
    }

    bool sse = t->isFloating();
    int id = nextLabel();
    std::string onStack = label("va.stack", id);
    std::string done = label("va.done", id);

    a_->ins("movl", (sse ? mem(4, "%rax") : mem("%rax")), reg("%ecx"));
    a_->ins("cmpl", imm((sse ? 176 : 48)), reg("%ecx"));
    a_->ins("jae", lbl(onStack));

    a_->ins("mov", mem(16, "%rax"), reg("%rdx"));
    a_->ins("add", reg("%rcx"), reg("%rdx"));
    a_->ins("addl", imm((sse ? 16 : 8)), reg("%ecx"));
    a_->ins("movl", reg("%ecx"), (sse ? mem(4, "%rax") : mem("%rax")));
    a_->ins("jmp", lbl(done));

    a_->defLabel(onStack);
    a_->ins("mov", mem(8, "%rax"), reg("%rdx"));
    a_->ins("lea", mem(8, "%rdx"), reg("%rcx"));
    a_->ins("mov", reg("%rcx"), mem(8, "%rax"));

    a_->defLabel(done);
    a_->ins("mov", reg("%rdx"), reg("%rax"));
    load(t);
}

void X86_64Linux::visit(const VaStart &n) {
    n.list().accept(*this);
    if (abi_.positional) {
        a_->ins("lea", mem(varOverflow_, "%rbp"), reg("%rcx"));
        a_->ins("mov", reg("%rcx"), mem("%rax"));
        return;
    }
    a_->ins("movl", imm(varGp_), mem("%rax"));
    a_->ins("movl", imm(varFp_), mem(4, "%rax"));
    a_->ins("lea", mem(varOverflow_, "%rbp"), reg("%rcx"));
    a_->ins("mov", reg("%rcx"), mem(8, "%rax"));
    a_->ins("lea", mem((-regSave_), "%rbp"), reg("%rcx"));
    a_->ins("mov", reg("%rcx"), mem(16, "%rax"));
}

void X86_64Linux::defineLabel(const std::string &l) { a_->defLabel(l); }
void X86_64Linux::defineStateLabel(const std::string &l) { a_->stateLabel(l); }
void X86_64Linux::jump(const std::string &l) { a_->ins("jmp", lbl(l)); }
void X86_64Linux::branchIfZero(const std::string &l) {
    a_->ins("cmp", immText("0"), reg("%rax"));
    a_->ins("je", lbl(l));
}
void X86_64Linux::branchIfNotZero(const std::string &l) {
    a_->ins("cmp", immText("0"), reg("%rax"));
    a_->ins("jne", lbl(l));
}
void X86_64Linux::caseBranch(long long v, const std::string &l) {
    if (v >= -2147483648L && v <= 2147483647L) {
        a_->ins("cmp", imm(v), reg("%rax"));
    } else {
        a_->ins("movabs", imm(v), reg("%rdx"));
        a_->ins("cmp", reg("%rdx"), reg("%rax"));
    }
    a_->ins("je", lbl(l));
}

void X86_64Linux::visit(const Return &n) {
    markLine(n);
    if (!n.hasValue()) {
        a_->ins("jmp", lbl(returnLabel_));
        return;
    }
    n.value().accept(*this);

    if (sretSlot_ != 0) {
        a_->ins("mov", local(sretSlot_), reg(abi_.scratch));
        copyBlock(n.value().type()->size(target_));
        a_->ins("mov", local(sretSlot_), reg("%rax"));
        a_->ins("jmp", lbl(returnLabel_));
        return;
    }

    if (n.value().type()->isStructOrUnion()) {
        const Type *t = n.value().type();
        int size = t->size(target_);

        if (abi_.aggregatesByReference) {
            a_->ins("mov", reg("%rax"), reg("%rcx"));
            if (size == 8)      a_->ins("mov", mem("%rcx"), reg("%rax"));
            else if (size == 4) a_->ins("movl", mem("%rcx"), reg("%eax"));
            else if (size == 2) a_->ins("movzwl", mem("%rcx"), reg("%eax"));
            else                a_->ins("movzbl", mem("%rcx"), reg("%eax"));
            a_->ins("jmp", lbl(returnLabel_));
            return;
        }

        std::vector<bool> lanes = classifyEightbytes(t, target_);
        const char *ret[2] = { "%rax", "%rdx" };
        const char *sret[2] = { "%xmm0", "%xmm1" };
        int nextInt = 0, nextSse = 0;
        a_->ins("mov", reg("%rax"), reg("%rcx"));
        for (std::size_t k = 0; k < lanes.size(); k++) {
            int off = static_cast<int>(k) * 8;
            int left = size - off;
            if (lanes[k]) {
                a_->ins(left >= 8 ? "movsd" : "movss", mem(off, "%rcx"), reg(sret[nextSse++]));
            } else if (left >= 8) {
                a_->ins("mov", mem(off, "%rcx"), reg(ret[nextInt++]));
            } else {
                loadTailToReg(ret[nextInt++], off, "%rcx", left);
            }
        }
    }
    a_->ins("jmp", lbl(returnLabel_));
}

std::string X86_64Linux::label(const char *kind, int id) const {
    return labelPrefix_ + kind + "." + std::to_string(id);
}

// The SysV pair: %rax holds the exception object and %rdx the selector, and
// both go into frame slots the parser already knows the numbers of.
void X86_64Linux::exceptionRegion(const std::string &begin, const std::string &end, const std::string &target) {
    if (optimizer_) optimizer_->exceptionRegion(begin, end, target);
}

void X86_64Linux::landingPad(int pointerSlot, int selectorSlot) {
    a_->ins("mov", reg("%rax"), local(pointerSlot));
    a_->ins("movl", reg("%edx"), local(selectorSlot));
}
// Where a throw out of a cleanup pad lands: std::terminate, as clang's
// __clang_call_terminate does, jumped over on the way through.
std::string X86_64Linux::terminatePad(int id) {
    a_->ins("jmp", lbl(label("terminateskip", id)));
    a_->defLabel(label("terminate", id));
    a_->ins("call", lbl("_ZSt9terminatev"));
    a_->defLabel(label("terminateskip", id));
    return label("terminate", id);
}

// The same table the arm64 backend writes, in this assembler's spelling; the
// layout and every encoding byte are documented there. What differs is the
// section, the label prefix, and a type_info reached through a stub, not @GOT.
static const Walker::LsdaSpelling kElfLsda = {
    ".L", ".section .gcc_except_table,\"a\",@progbits", false, ".L", ".DW.stub"
};

void X86_64Linux::emitLsda(const std::string &symbol, bool mergeable) {
    std::string &o = out_;
    // A mergeable function's table joins its group: gcc's `.gcc_except_table.<sym>`.
    Walker::LsdaSpelling sp = kElfLsda;
    const std::string grouped = ".section .gcc_except_table." + symbol +
                                ",\"aG\",@progbits," + symbol + ",comdat";
    if (mergeable) sp.section = grouped.c_str();
    o += lsdaTable(sp, symbol, lsdaTypes_);

    // **The two objects an ELF table refers to indirectly.** The type table holds
    // offsets to *pointers*, since a direct reference to one in another shared
    // object needs a text relocation; the personality is a weak hidden comdat.
    for (std::size_t i = 0; i < lsdaTypes_.size(); i++) {
        if (lsdaTypes_[i].empty()) continue;
        bool had = false;
        for (std::size_t k = 0; k < lsdaStubs_.size(); k++)
            if (lsdaStubs_[k] == lsdaTypes_[i]) had = true;
        if (had) continue;
        lsdaStubs_.push_back(lsdaTypes_[i]);
        o += "  .data\n";
        o += "  .p2align 3\n";
        o += ".L" + lsdaTypes_[i] + ".DW.stub:\n";
        o += "  .quad " + lsdaTypes_[i] + "\n";
    }
    if (!lsdaPersonality_) {
        lsdaPersonality_ = true;
        o += "  .hidden DW.ref.__gxx_personality_v0\n";
        o += "  .weak DW.ref.__gxx_personality_v0\n";
        o += "  .section .data.DW.ref.__gxx_personality_v0,\"awG\","
             "@progbits,DW.ref.__gxx_personality_v0,comdat\n";
        o += "  .p2align 3\n";
        o += "  .type DW.ref.__gxx_personality_v0,@object\n";
        o += "  .size DW.ref.__gxx_personality_v0, 8\n";
        o += "DW.ref.__gxx_personality_v0:\n";
        o += "  .quad __gxx_personality_v0\n";
    }
    o += gnu_.currentText();   // the function's own section, for the labels -g adds after it
}

std::string X86_64Linux::userLabel(const std::string &name) const {
    return labelPrefix_ + "user." + name;
}

// **The GNU and COFF spellings only.** The MASM path has its own spelling and
// its own funclet cutting, and is written as it was.
void X86_64Linux::setOptimize(int level) {
    if (level <= 0 || optimizer_) return;
    optimizer_.reset(new Optimizer(*a_, abi_, level));
    if (optimizer_->costs().inlines()) inliner_.reset(new Inliner(optimizer_->costs()));
    a_ = optimizer_.get();
}

void X86_64Linux::finishChunk() {
    chunks_.push_back(out_);
    out_.clear();
}

namespace { std::vector<const Call *> callsIn(const Node &n, std::vector<const Call *> *outer = nullptr); }

// A call with stack arguments writes them above the shadow space, and a callee
// walked in place may make any call at all.
bool X86_64Linux::mayWriteArea(const Expr &e) const {
    for (const Call *c : callsIn(e)) {
        const int words = placeCall(*c).stackWords;
        if (words > 0 || inlineTarget(*c, words) != nullptr) return true;
    }
    return false;
}

void X86_64Linux::emit(const Function &fn) {
    depth_ = 0;
    resetLabels();
    // Labels are built from the symbol rather than the name: two overloads
    // share a name and must not share a label.
    labelPrefix_ = ".L." + fn.symbol() + ".";
    returnLabel_ = ".L.return." + fn.symbol();

    a_->functionBegin(fn.symbol(), !fn.isStatic(), fn.isInline());
    if (optimizer_)
        optimizer_->returnsPair(!abi_.aggregatesByReference && fn.returns()->isStructOrUnion() &&
                                fn.returns()->size(target_) > 8);
    if (fn.isInline()) a_->weakDefinition(fn.symbol());
    // A second name for the same code - see Function::alias. Emitted as a
    // label at the same address, which is what makes it the same function
    // rather than a second copy of it.
    if (!fn.alias().empty()) {
        if (!fn.isStatic()) a_->globl(fn.alias());
        if (fn.isInline()) a_->weakDefinition(fn.alias());
        a_->defLabel(fn.alias());
    }
    if (const Source *src = lineSource()) {
        Source::Place at = src->locate(fn.pos());
        DwarfFunction d;
        d.name = fn.name();
        d.begin = ".Lfunc.begin." + fn.symbol();
        d.end = ".Lfunc.end." + fn.symbol();
        d.file = at.file + 1;
        d.line = at.line;
        d.external = !fn.isStatic();
        d.returns = fn.returns();
        d.locals = &fn.locals();
        dwarfFns_.push_back(d);
        resetBlocks(fn.blocks());
        a_->defLabel(d.begin);
    } else if (fn.hasLandingPads() && !usesFunclets()) {
        // The call-site table measures from here, whether or not there is
        // debug information. A funclet target has no call-site table and no
        // use for the label - and MASM will not take one shaped like this.
        a_->defLabel(".Lfunc.begin." + fn.symbol());
    }
    clearCallSites();
    clearMsTries();

    // **A frame cut into funclets cannot grow once its first funclet is out**,
    // so it reserves room for any callee it may walk in place from the start.
    current_ = &fn;
    inlineReserve_ = fn.hasLandingPads() && usesFunclets() && inlining() ? inliner_->largestFrame() : 0;
    frameSize_ = fn.frameSize() + inlineReserve_;
    fnSymbol_ = fn.symbol();
    fnMergeable_ = fn.isInline();
    markLine(fn.pos());
    if (optimizer_) optimizer_->frame(scalarsOf(fn));
    // The shadow space and the widest stack arguments of a call not inside
    // another's operands, in 16s. A nested call is evaluated with the outer
    // one's values pushed, so it never finds the area; counting it only made
    // frames larger, and with them the displacements into the frame - 1.6 KB
    // of .text at -O2, measured.
    outgoing_ = 0;
    if (abi_.shadowBytes > 0) {
        std::vector<const Call *> outer;
        const std::vector<const Call *> calls = callsIn(fn.body(), &outer);
        int words = 0;
        for (const Call *c : outer) words = std::max(words, placeCall(*c).stackWords);
        if (!calls.empty()) outgoing_ = (abi_.shadowBytes + 8 * words + 15) & ~15;
    }
    floorDepth_ = 0;
    tempsAllowed_ = optimizer_ && !(fn.hasLandingPads() && usesFunclets());
    tempDepth_ = tempHigh_ = 0;
    a_->prologue(frameSize_,
                 fn.hasLandingPads() ? ".Lexception." + fn.symbol()
                                     : std::string(),
                 outgoing_);

    receiveParameters(fn);

    walkBody(fn);
    if (optimizer_) optimizer_->temporaries(tempHigh_);
    // **rsp is restored *from rbp*, never by adding to itself.** Resuming after a
    // catch it holds whatever the runtime left, and adding the frame size landed
    // on the unwind-help slot, so `ret` took -2. The renderer adds the size.
    if (localsAboveFrameBase()) {
        a_->ins("lea", mem(0, "%rbp"), reg("%rsp"));
    } else {
        a_->ins("mov", reg("%rbp"), reg("%rsp"));
    }
    a_->ins("pop", reg("%rbp"));
    a_->ins("ret");
    // Before .cfi_endproc, because the last call-site range measures to it.
    if (!lineSource() && !callSites().empty() && !usesFunclets())
        a_->defLabel(".Lfunc.end." + fn.symbol());
    // The tables are written below; tell the spelling whether there are any,
    // so its unwind info does not name a FuncInfo that never appears.
    if (target_.microsoftNames()) a_->noteHasEh(!msStates().empty());
    a_->functionEnd(fn.symbol());
    // The tables below measure slots from the frame as the passes left it.
    if (optimizer_) frameSize_ = optimizer_->frameSize();
    emitExceptionTables(fn);
    if (lineSource()) {
        a_->defLabel(".Lfunc.end." + fn.symbol());

        dwarfFns_.back().blocks = blocks();
    }

    if (depth_ != 0 || tempDepth_ != 0) {
        std::fprintf(stderr, "codegen: stack depth %d and %d temporaries at the end of %s\n",
                     depth_, tempDepth_, fn.name().c_str());
        std::exit(1);
    }
    finishChunk();
}

// **Parameters into their slots**, for a function emitted whole or walked in place.
void X86_64Linux::receiveParameters(const Function &fn) {
    // The definition side of the same rule: for a member function on the
    // Microsoft ABI the hidden return pointer arrives in the *second* integer
    // register, `this` having taken the first.
    sretSlot_ = fn.sretSlot();
    const bool msThisFirst = sretSlot_ != 0 && abi_.positional && fn.hasThis();
    if (sretSlot_ != 0)
        a_->ins("mov", reg(abi_.intRegs[msThisFirst ? 1 : 0]), local(sretSlot_));

    regSave_ = fn.regSaveSlot();
    if (fn.isVariadic() && abi_.positional) {
        for (int i = 0; i < abi_.intCount; i++)
            a_->ins("mov", reg(abi_.intRegs[i]), mem((16 + i * 8), "%rbp"));
        regSave_ = 0;
    } else if (regSave_ != 0) {
        for (int i = 0; i < 6; i++)
            a_->ins("mov", reg(abi_.intRegs[i]), mem((i * 8 - regSave_), "%rbp"));
        std::string done = ".L.novec." + fn.symbol();
        a_->ins("testb", reg("%al"), reg("%al"));
        a_->ins("je", lbl(done));
        for (int i = 0; i < 8; i++)
            a_->ins("movaps", reg("%xmm" + std::to_string(i)), mem((48 + i * 16 - regSave_), "%rbp"));
        a_->defLabel(done);
    }

    const std::vector<Param> &ps = fn.params();
    // **The same walk the caller made, made once.** placeArguments answers
    // where each parameter is; what is left here is reading it out of there,
    // and the base is this side's.
    std::vector<const Type *> ptypes;
    ptypes.reserve(ps.size());
    for (std::size_t i = 0; i < ps.size(); i++) ptypes.push_back(ps[i].type);
    const Placement plan = placeArguments(ptypes, fn.hasThis(), sretSlot_ != 0);
    const int stackBase = 16 + abi_.shadowBytes;

    bool byRef = abi_.aggregatesByReference;
    for (std::size_t i = 0; i < ps.size(); i++) {
        const ArgPlace &pl = plan.args[i];
        const Type *pt = ps[i].type;
        if (byRef && pt->isStructOrUnion()) {
            bool inReg = !pl.inMemory;
            std::string src;
            if (inReg) {
                src = abi_.intRegs[pl.regs[0]];
            } else {
                a_->ins("mov", mem(stackBase + pl.stackOffset, "%rbp"), reg("%rax"));
                src = "%rax";
            }
            int size = pt->size(target_);
            int to = -ps[i].offset;
            if (msInRegister(size)) {
                if (src != "%rax") a_->ins("mov", reg(src), reg("%rax"));
                if (size == 8)      a_->ins("movq", reg("%rax"), mem(to, "%rbp"));
                else if (size == 4) a_->ins("movl", reg("%eax"), mem(to, "%rbp"));
                else if (size == 2) a_->ins("movw", reg("%ax"), mem(to, "%rbp"));
                else                a_->ins("movb", reg("%al"), mem(to, "%rbp"));
            } else {
                msCopyToSlot(pt, ps[i].offset, src.c_str());
            }
            continue;
        }
        if (pl.inMemory) {
            int size = pt->size(target_);
            int slots = pl.stackWords;
            for (int k = 0; k < slots; k++) {
                int from = stackBase + pl.stackOffset + k * 8;
                int to = k * 8 - ps[i].offset;
                int left = size - k * 8;
                if (left >= 8) {
                    a_->ins("mov", mem(from, "%rbp"), reg("%rax"));
                    a_->ins("movq", reg("%rax"), mem(to, "%rbp"));
                } else {
                    copyTailMem(from, to, left);
                }
            }
            continue;
        }

        if (ps[i].type->isStructOrUnion()) {
            int size = ps[i].type->size(target_);
            for (std::size_t k = 0; k < pl.lanes.size(); k++) {
                int off = static_cast<int>(k) * 8 - ps[i].offset;
                int left = size - static_cast<int>(k) * 8;
                if (pl.lanes[k]) {
                    a_->ins(left >= 8 ? "movsd" : "movss", reg(abi_.sseRegs[pl.regs[k]]), mem(off, "%rbp"));
                } else {
                    a_->ins("mov", reg(abi_.intRegs[pl.regs[k]]), reg("%rax"));
                    if (left >= 8) a_->ins("movq", reg("%rax"), mem(off, "%rbp"));
                    else           storeTailFromReg("%rax", off, "%rbp", left);
                }
            }
            continue;
        }
        if (ps[i].type->isFloating()) {
            a_->ins("movsd", reg(abi_.sseRegs[pl.regs[0]]), reg("%xmm0"));
        } else {
            a_->ins("mov", reg(abi_.intRegs[pl.regs[0]]), reg("%rax"));
        }
        storeAt(ps[i].type, ps[i].offset);
    }

    varGp_ = plan.intsUsed * 8;
    varFp_ = 48 + plan.ssesUsed * 16;
    varOverflow_ = abi_.positional ? 16 + plan.intsUsed * 8
                                   : stackBase + plan.stackWords * 8;
}

std::vector<opt::Local> X86_64Linux::scalarsOf(const Function &fn) const {
    std::vector<opt::Local> scalars;
    for (const Local &l : fn.locals())
        if (l.staticName.empty() && (l.type->isInteger() || l.type->isPointer()))
            scalars.push_back(opt::Local{-static_cast<long long>(l.offset), l.type->size(target_)});
    return scalars;
}

namespace {

// **The calls a body or an expression makes**, which the outgoing area is
// sized by. A call this misses keeps its own allocation, so an incomplete
// answer costs bytes and never correctness; a handler's calls count, since a
// funclet is walked where its try is.
class CallScan final : public Visitor {
public:
    std::vector<const Call *> calls;
    std::vector<const Call *> outer;     // not inside another call's operands
    int inCall = 0;
    void visit(const Num &) override {}
    void visit(const Var &) override {}
    void visit(const StrLit &) override {}
    void visit(const Goto &) override {}
    void visit(const Break &) override {}
    void visit(const Continue &) override {}
    void visit(const FuncletLeave &) override {}
    void visit(const Call &n) override {
        calls.push_back(&n);
        if (inCall == 0) outer.push_back(&n);
        ++inCall;
        if (n.callee() != nullptr) n.callee()->accept(*this);
        for (const ExprPtr &a : n.args()) a->accept(*this);
        --inCall;
    }
    void visit(const VaStart &n) override { n.list().accept(*this); }
    void visit(const VaArg &n) override { n.list().accept(*this); }
    void visit(const Assign &n) override { n.target().accept(*this); n.value().accept(*this); }
    void visit(const Unary &n) override { n.operand().accept(*this); }
    void visit(const Binary &n) override { n.lhs().accept(*this); n.rhs().accept(*this); }
    void visit(const Postfix &n) override { n.target().accept(*this); }
    void visit(const Cast &n) override { n.value().accept(*this); }
    void visit(const Comma &n) override { n.left().accept(*this); n.right().accept(*this); }
    void visit(const Conditional &n) override {
        n.cond().accept(*this); n.thenArm().accept(*this); n.elseArm().accept(*this);
    }
    void visit(const MemberAccess &n) override { n.object().accept(*this); }
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
        for (const StmtPtr &s : n.body()) s->accept(*this);
        if (n.hasPad()) n.pad().accept(*this);
        if (n.cleanup() != nullptr) n.cleanup()->accept(*this);
        for (const MsHandler &h : n.handlers())
            if (h.body != nullptr) h.body->accept(*this);
    }
};

std::vector<const Call *> callsIn(const Node &n, std::vector<const Call *> *outer) {
    CallScan scan;
    n.accept(scan);
    if (outer != nullptr) *outer = scan.outer;
    return scan.calls;
}

}

// **A direct call to a function of this unit, walked in its place** where
// the site is safe - not itself inside a callee walked in place, nothing
// on the stack, a frame that fits the room a funclet-cut caller reserved -
// and the inliner's budgets say it is worth it.
const Function *X86_64Linux::inlineTarget(const Call &n, int stackSlots) const {
    if (!inlining() || inPlace_ || current_ == nullptr || n.callee() != nullptr || stackSlots != 0) return nullptr;
    const auto it = bodies_.find(n.symbol());
    if (it == bodies_.end()) return nullptr;
    const Function &callee = *it->second;
    const bool reserved = current_->hasLandingPads() && usesFunclets();
    if (reserved && ((callee.frameSize() + 15) & ~15) > inlineReserve_) return nullptr;
    return inliner_->allows(n) ? &callee : nullptr;
}

bool X86_64Linux::inlining() const { return inliner_ && !lineSource(); }

// The callee's walk borrows the caller's state for its own, and gives it
// back; its frame the optimizer moves below the caller's.
void X86_64Linux::walkInPlace(const Function &fn) {
    const int depth = depth_, sret = sretSlot_, regSave = regSave_;
    const int gp = varGp_, fp = varFp_, overflow = varOverflow_;
    const std::string ret = returnLabel_;
    floorDepth_ += depth;
    depth_ = 0;
    returnLabel_ = label("inline", nextLabel());
    optimizer_->jumpOnly(returnLabel_);
    inPlace_ = true;
    optimizer_->inlineBegin(current_->frameSize(), fn.frameSize(), scalarsOf(fn));
    receiveParameters(fn);
    walkBody(fn);
    optimizer_->inlineEnd();
    inPlace_ = false;
    floorDepth_ -= depth;
    depth_ = depth;
    sretSlot_ = sret;
    regSave_ = regSave;
    varGp_ = gp;
    varFp_ = fp;
    varOverflow_ = overflow;
    returnLabel_ = ret;
}

// The body, the value a function that falls off its end returns, and the
// return label every `return` jumps to.
void X86_64Linux::walkBody(const Function &fn) {
    const std::vector<Param> &ps = fn.params();
    fn.body().accept(*this);

    if (sretSlot_ != 0)                     a_->ins("mov", local(sretSlot_), reg("%rax"));
    else if (isX87(fn.returns()))           a_->ins("fldz");
    else if (fn.returns()->isFloating())    a_->ins("pxor", reg("%xmm0"), reg("%xmm0"));
    else                                    a_->ins("mov", immText("0"), reg("%rax"));
    a_->defLabel(returnLabel_);
    // **A Microsoft constructor or deleting destructor returns `this` in
    // RAX**, and cl's callers read it - `new V(4)` hands back the constructor's
    // RAX as it stands. Measured 2026-09-17; after the label, for every path.
    if (abi_.positional && fn.hasThis() && !ps.empty() &&
        (fn.symbol().compare(0, 3, "??0") == 0 ||
         fn.symbol().compare(0, 4, "??_G") == 0 ||
         fn.symbol().compare(0, 4, "??_E") == 0))
        a_->ins("mov", local(ps[0].offset), reg("%rax"));
}

// **A funclet is a slice of the ordinary output, lifted.** Walking the handler
// appends its code like any other, so remembering where that began and cutting
// back to it gives the body exactly - and the code generator knows none of it.
std::string X86_64Linux::beginFunclet() {
    if (optimizer_) optimizer_->funcletBegin();
    inFunclet_ = true;
    atOutput([this] { funcletMark_ = out_.size(); });
    funcletSymbol_ = "$" + std::string(funcletKind_).substr(1) +
                     std::to_string(funcletIndex_++) + "$" + fnSymbol_;
    return funcletSymbol_;
}

void X86_64Linux::endCleanupFunclet() {
    closeFunclet(std::string());
    funcletKind_ = "$catch$";
}

// **The resume label goes through labelText like every table entry does.**
void X86_64Linux::endFunclet(const std::string &resume) {
    closeFunclet("  lea " + a_->labelText(resume) + "(%rip), %rax\n");
}

// An early exit: the continuation in rax, then the funclet's own epilogue,
// which closeFunclet labels. The jump leaves the stream, so nothing is dead.
void X86_64Linux::funcletLeave(const std::string &label) {
    a_->ins("lea", rip(label), reg("%rax"));
    a_->ins("jmp", lbl("$LNleave$" + funcletSymbol_));
}

// Write -2 into the runtime's scratch word: the personality routine reads it
// through the FuncInfo's dispUnwindHelp to know how far this frame had got.
void X86_64Linux::storeUnwindHelp(int slot) {
    if (optimizer_) optimizer_->sharedSlot(-slot, 8);
    a_->ins("movq", immText("-2"), mem(-slot, "%rbp"));
}

// **The funclet's own frame, and the assembler writes its unwind data.**
void X86_64Linux::closeFunclet(const std::string &tail) {
    inFunclet_ = false;
    const std::string q = "\"" + funcletSymbol_ + "\"";
    const std::string b = "\"$LNbeg$" + funcletSymbol_ + "\"";
    const std::string e = "\"$LNend$" + funcletSymbol_ + "\"";
    const std::string pu = "\"$LNpush$" + funcletSymbol_ + "\"";
    const std::string pr = "\"$LNprolog$" + funcletSymbol_ + "\"";
    const std::string u = "\"$unwind$" + funcletSymbol_ + "\"";

    // **A funclet of a mergeable parent is mergeable with it.**
    const std::string assoc =
        fnMergeable_ ? ",associative," + a_->labelText(fnSymbol_) : std::string();

    // The text around the body: the head, then the body as written out,
    // then the tail with the unwind data.
    std::string head, f;
    // **Not `.globl`.**
    head += "\n  .section .text$x,\"xr\"" + assoc + "\n";
    head += q + ":\n";
    head += b + ":\n";
    head += "  movq %rdx, 16(%rsp)\n";
    head += "  push %rbp\n";
    head += pu + ":\n";
    head += "  sub $32, %rsp\n";
    head += pr + ":\n";
    // rdx is the establisher frame, which on this target is exactly the parent's rbp - the two
    // became one thing when the frame pointer moved to the bottom of the allocation - so the
    // handler reaches the parent's locals with no adjustment.
    head += "  mov %rdx, %rbp\n";
    f += tail;
    f += "\"$LNleave$" + funcletSymbol_ + "\":\n";
    f += "  add $32, %rsp\n";
    f += "  pop %rbp\n";
    f += "  ret\n";
    f += e + ":\n";

    // **Written out rather than left to `.seh_*`**, for the reason
    // CoffSpelling::prologue records: the directives cannot carry handler data,
    // and a funclet's names the parent's FuncInfo. Two codes, no frame register.
    f += "  .section .xdata,\"dr\"" + assoc + "\n";
    f += "  .p2align 3\n";
    f += u + ":\n";
    f += "  .byte 0x19\n";
    f += "  .byte " + pr + "-" + b + "\n";
    f += "  .byte 2\n";
    f += "  .byte 0\n";
    // UWOP_ALLOC_SMALL for 32 bytes: 2 with (32/8 - 1) in the high nibble.
    f += "  .byte " + pr + "-" + b + "\n";
    f += "  .byte 0x32\n";
    f += "  .byte " + pu + "-" + b + "\n";
    f += "  .byte 0x50\n";
    f += "  .long __CxxFrameHandler3@IMGREL\n";
    // The funclet shares the parent's FuncInfo: one description of the try.
    f += "  .long \"$cppxdata$" + fnSymbol_ + "\"@IMGREL\n";
    f += "  .text\n";

    // **`.pdata$x`, so the funclets' entries sort after every function's**: an inline
    // function's COMDAT code lands after `.text`, and a funclet entry among the
    // functions' put its entry out of order - a throw from inside it found no handler.
    std::string pdata;
    pdata += "  .section .pdata$x,\"dr\"" + assoc + "\n";
    pdata += "  .p2align 2\n";
    pdata += "  .long " + b + "@IMGREL\n";
    pdata += "  .long " + e + "@IMGREL\n";
    pdata += "  .long " + u + "@IMGREL\n";
    // **The body is lifted out of the output where it was written**, which
    // with an optimizer in front is after the function it belongs to.
    atOutput([this, head, f, pdata] {
        const std::string body = out_.substr(funcletMark_);
        out_.resize(funcletMark_);
        funclets_ += head + body + f;
        funcletPdata_ += pdata;
    });
    if (optimizer_) optimizer_->funcletEnd();
}

// **The FH3 tables for a frame that only cleans up.**
void X86_64Linux::emitCoffEhTables(const Function &fn) {
    (void)fn;
    const std::string m = fnSymbol_;
    const std::vector<MsState> &states = msStates();
    const std::vector<MsTryBlock> &tries = msTryBlocks();

    std::string o;
    o += funclets_;
    funclets_.clear();
    funcletIndex_ = 0;

    o += "\n  .section .xdata,\"dr\"\n";
    o += "  .p2align 2\n";
    o += "\"$cppxdata$" + m + "\":\n";
    o += "  .long 0x19930522\n";
    o += "  .long " + std::to_string(states.size()) + "\n";
    o += "  .long \"$stateUnwindMap$" + m + "\"@IMGREL\n";
    o += "  .long " + std::to_string(tries.size()) + "\n";
    if (tries.empty()) o += "  .long 0\n";
    else o += "  .long \"$tryMap$" + m + "\"@IMGREL\n";
    o += "  .long " + std::to_string(1 + msIpRows().size() + msFuncletRows().size()) + "\n";
    o += "  .long \"$ip2state$" + m + "\"@IMGREL\n";
    o += "  .long " + std::to_string(establisherOffset(msUnwindHelpSlot())) + "\n";
    o += "  .long 0\n";                     // no exception specification
    o += "  .long 1\n";                     // EHFlags: compiled with /EHsc

    // toState, then the action: a cleanup's funclet, or nothing.
    o += "\"$stateUnwindMap$" + m + "\":\n";
    for (std::size_t k = 0; k < states.size(); k++) {
        o += "  .long " + std::to_string(states[k].toState) + "\n";
        if (states[k].action.empty()) o += "  .long 0\n";
        else o += "  .long " + a_->labelText(states[k].action) + "@IMGREL\n";
    }

    if (!tries.empty()) o += "\"$tryMap$" + m + "\":\n";
    for (std::size_t k = 0; k < tries.size(); k++) {
        o += "  .long " + std::to_string(tries[k].tryLow) + "\n";
        o += "  .long " + std::to_string(tries[k].tryHigh) + "\n";
        o += "  .long " + std::to_string(tries[k].catchHigh) + "\n";
        o += "  .long " + std::to_string(tries[k].handlers.size()) + "\n";
        o += "  .long \"$handlerMap$" + std::to_string(k) + "$" + m + "\"@IMGREL\n";
    }

    for (std::size_t k = 0; k < tries.size(); k++) {
        o += "\"$handlerMap$" + std::to_string(k) + "$" + m + "\":\n";
        for (std::size_t i = 0; i < tries[k].handlers.size(); i++) {
            const MsHandlerRow &h = tries[k].handlers[i];
            // 0x40 is HT_IsCatchAll and names no type; 0x08 is HT_IsReference,
            // which puts the object's address in the slot rather than a copy.
            o += "  .long ";
            o += h.descriptor.empty() ? "0x40\n"
                                      : std::to_string((h.byReference ? 8 : 0) |
                                                       (h.constPointer ? 1 : 0)) + "\n";
            if (h.descriptor.empty()) o += "  .long 0\n";
            else o += "  .long " + a_->labelText(h.descriptor) + "@IMGREL\n";
            o += "  .long " + std::to_string(h.objectSlot == 0
                                  ? 0 : establisherOffset(h.objectSlot)) + "\n";
            o += "  .long " + a_->labelText(h.funclet) + "@IMGREL\n";
            // dispFrame: where the funclet saved the parent's frame pointer, from
            // its own establisher - [rsp+16] at entry, past `push rbp; sub rsp,32`.
            // Read only when an exception passes *through* a catch funclet. cl: 038H.
            o += "  .long 56\n";
        }
    }

    // Where each state begins, in address order: the body's rows as walked,
    // then the funclets, each wholly inside its own handler state.
    o += "\"$ip2state$" + m + "\":\n";
    o += "  .long \"$LNbeg$" + m + "\"@IMGREL\n";
    o += "  .long -1\n";
    for (std::size_t k = 0; k < msIpRows().size(); k++) {
        o += "  .long " + a_->labelText(msIpRows()[k].label) + "@IMGREL\n";
        o += "  .long " + std::to_string(msIpRows()[k].state) + "\n";
    }
    for (std::size_t k = 0; k < msFuncletRows().size(); k++) {
        o += "  .long " + a_->labelText(msFuncletRows()[k].label) + "@IMGREL\n";
        o += "  .long " + std::to_string(msFuncletRows()[k].state) + "\n";
    }
    o += "  .text\n";
    out_ += o;
}

// **The five objects the Microsoft ABI wants per class with a vftable**, in
// GNU spelling.
void X86_64Linux::emitCoffClassRtti(const Program &program) {
    if (program.rtti.empty()) return;
    std::string &o = out_;

    // The chain of every class named, closed and deduplicated: a class's array
    // lists its bases' descriptors, so a base needs the full set of records
    // even when nothing in this file names it.
    std::vector<const Type *> all;
    for (std::size_t i = 0; i < program.rtti.size(); i++)
        for (const Type *k = program.rtti[i]; k != nullptr; k = k->base()) {
            bool had = false;
            for (std::size_t j = 0; j < all.size(); j++) if (all[j] == k) had = true;
            if (!had) all.push_back(k);
        }

    for (std::size_t i = 0; i < all.size(); i++) {
        MicrosoftRtti n;
        std::string why;
        if (!microsoftClassRttiNames(all[i], &n, &why)) continue;

        int contained = 0;
        for (const Type *k = all[i]->base(); k != nullptr; k = k->base())
            contained++;

        const std::string d = a_->labelText(n.descriptor);
        const std::string bd = a_->labelText(n.baseDescriptor);
        const std::string ar = a_->labelText(n.array);
        const std::string hi = a_->labelText(n.hierarchy);
        const std::string lo = a_->labelText(n.locator);

        // **The descriptor is writable** - its spare word caches the undecorated name
        // __std_type_info_name makes - so `.data`, as cl and clang have it; the four
        // records below it are `.rdata$r`, and not `.data$r`.
        coffRecord(".data", d, 3, "dw");
        o += "  .quad \"??_7type_info@@6B@\"\n";
        o += "  .quad 0\n";
        o += "  .asciz \"" + n.decorated + "\"\n";
        msDescriptors_.insert(n.descriptor);

        // Where this class sits inside itself: at the top, never virtual.
        coffRecord(".rdata$r", bd, 2);
        o += "  .long " + d + "@IMGREL\n";
        o += "  .long " + std::to_string(contained) + "\n";
        o += "  .long 0\n";              // mdisp
        o += "  .long -1\n";             // pdisp - there is no vbtable
        o += "  .long 0\n";              // vdisp
        o += "  .long 0x40\n";           // attributes
        o += "  .long " + hi + "@IMGREL\n";

        coffRecord(".rdata$r", ar, 2);
        o += "  .long " + bd + "@IMGREL\n";
        for (const Type *k = all[i]->base(); k != nullptr; k = k->base()) {
            MicrosoftRtti b;
            if (!microsoftClassRttiNames(k, &b, &why)) break;
            o += "  .long " + a_->labelText(b.baseDescriptor) + "@IMGREL\n";
        }
        o += "  .long 0\n";

        coffRecord(".rdata$r", hi, 2);
        o += "  .long 0\n";
        o += "  .long 0\n";              // attributes - no MI, no virtual bases
        o += "  .long " + std::to_string(contained + 1) + "\n";
        o += "  .long " + ar + "@IMGREL\n";

        // The locator names itself, which is how the runtime recovers the image
        // base every other field is relative to.
        coffRecord(".rdata$r", lo, 2);
        o += "  .long 1\n";
        o += "  .long 0\n";              // the vfptr's offset in the object
        o += "  .long 0\n";              // cdOffset
        o += "  .long " + d + "@IMGREL\n";
        o += "  .long " + hi + "@IMGREL\n";
        o += "  .long " + lo + "@IMGREL\n";
        o += "  .text\n";
    }
}

void X86_64Linux::emitGlobal(const Global &g, Segment seg) {
    int size = g.type->size(target_);
    if (!g.isStatic) a_->globl(g.symbol);
    if (g.isInline) a_->weakDefinition(g.symbol);

    if (abi_.elfSymbolAttributes) {
        a_->objectType(g.symbol);
        a_->objectSize(g.symbol, size);
    }
    a_->align(g.align > objectAlign(g.type, target_) ? g.align : objectAlign(g.type, target_));
    // The word in front, where there is one: laid down after the alignment so
    // that it is the label - not the block - that ends up aligned, which is
    // what makes `vftable - 8` the locator the runtime reads.
    if (!g.prefixWord.empty()) a_->dataSym(g.prefixWord, 0);
    a_->defLabel(g.symbol);

    if (seg == Segment::Bss) { a_->zero(size); return; }

    int at = 0;
    for (const GlobalPiece &p : g.init) {
        if (p.offset > at) a_->zero((p.offset - at));

        if (!p.symbol.empty()) {
            a_->dataSym(p.symbol, p.value);
            at = p.offset + p.size;
            continue;
        }

        switch (p.size) {
        case 1: a_->dataInt(1, p.value); break;
        case 2: a_->dataInt(2, p.value); break;
        case 4: a_->dataInt(4, p.value); break;
        default: a_->dataInt(8, p.value); break;
        }
        at = p.offset + p.size;
    }
    if (at < size) a_->zero((size - at));
}

void X86_64Linux::emitData(const Program &program) {
    bool rodataOpen = false;
    if (!program.strings.empty()) {
        a_->rodataSection();
        rodataOpen = true;

        for (const StringLit &s : program.strings) {
            a_->defLabel(s.label);
            a_->dataBytes(s.bytes);
        }
    }

    struct Bucket { Segment seg; bool alreadyOpen; };
    const Bucket order[] = {
        { Segment::Const, rodataOpen },

        { Segment::ConstRelocated, rodataOpen },
        { Segment::Data,  false },
        { Segment::Bss,   false },
    };
    for (const Bucket &b : order) {
        bool opened = b.alreadyOpen;
        for (const Global &g : program.globals) {
            if (segmentFor(g) != b.seg) continue;
            if (!opened) {
                if (b.seg == Segment::Const ||
                    b.seg == Segment::ConstRelocated) a_->rodataSection();
                else if (b.seg == Segment::Data) a_->dataSection();
                else                             a_->bssSection();
                opened = true;
            }
            emitGlobal(g, b.seg);
        }
    }
}

// The four objects a Microsoft `throw` hands the runtime, spelled for GNU-as:
// the type descriptor, one catchable type, the array listing it, and the
// ThrowInfo itself.
void X86_64Linux::emitCoffThrowInfo(const Program &program) {
    if (program.msThrows.empty()) return;
    std::string &o = out_;
    std::set<std::string> catchables;

    for (std::size_t i = 0; i < program.msThrows.size(); i++) {
        const MicrosoftThrow &n = program.msThrows[i];
        for (std::size_t k = 0; k < n.catchables.size(); k++) {
            const MicrosoftThrow::Catchable &c = n.catchables[k];
            if (msDescriptors_.insert(c.descriptor).second) {
                coffRecord(".data", a_->labelText(c.descriptor), 3, "dw");
                o += "  .quad \"??_7type_info@@6B@\"\n";
                o += "  .quad 0\n";
                o += "  .asciz \"" + c.decorated + "\"\n";
            }
            if (!n.thrown || !catchables.insert(c.name).second) continue;
            coffRecord(".xdata$x", a_->labelText(c.name), 2);
            o += "  .long " + std::string(c.simple ? "1" : "0") + "\n";   // properties
            o += "  .long " + a_->labelText(c.descriptor) + "@IMGREL\n";
            o += "  .long " + std::to_string(c.mdisp) + "\n";          // mdisp
            o += "  .long -1\n";                                        // pdisp: no vbtable
            o += "  .long 0\n";                                         // vdisp
            o += "  .long " + std::to_string(c.size) + "\n";           // sizeOrOffset
            if (c.copyCtor.empty()) o += "  .long 0\n";                 // copyFunction
            else o += "  .long " + a_->labelText(c.copyCtor) + "@IMGREL\n";
        }
        if (!n.thrown) continue;
        const std::string ar = a_->labelText(n.array);
        coffRecord(".xdata$x", ar, 2);
        o += "  .long " + std::to_string(n.catchables.size()) + "\n";
        for (std::size_t k = 0; k < n.catchables.size(); k++)
            o += "  .long " + a_->labelText(n.catchables[k].name) + "@IMGREL\n";
        coffRecord(".xdata$x", a_->labelText(n.info), 2);
        o += "  .long " + std::string(n.isConst ? "1" : "0") + "\n";     // attributes
        if (n.destructor.empty()) o += "  .long 0\n";                   // pmfnUnwind
        else o += "  .long " + a_->labelText(n.destructor) + "@IMGREL\n";
        o += "  .long 0\n";                                             // pForwardCompat
        o += "  .long " + ar + "@IMGREL\n";
    }
}

// **Each record is a COMDAT of its own, and public**, as cl writes them: a
// plain `.rdata$r` is private to its object, so every unit that named a type
// kept a copy and the linker had nothing to fold on. `discard` is
// IMAGE_COMDAT_SELECT_ANY, the choice functionBegin makes for an inline
// function; the `.globl` is what lets one unit's copy stand for another's.
void X86_64Linux::coffRecord(const char *section, const std::string &label,
                             int p2align, const char *flags) {
    out_ += std::string("  .section ") + section + ",\"" + flags + "\",discard," + label + "\n";
    out_ += "  .globl " + label + "\n";
    out_ += "  .p2align " + std::to_string(p2align) + "\n";
    out_ += label + ":\n";
}

void X86_64Linux::run(const Program &program) {
    // The Microsoft RTTI records, where this generator serves that target.
    // The MASM path emits its own in the same place and for the same reason:
    // no other target has anything like them.
    if (target_.microsoftNames() && !emitsOwnRtti()) {
        emitCoffClassRtti(program);
        emitCoffThrowInfo(program);
    }

    std::vector<std::string> defined;
    for (const Function &fn : program.functions) defined.push_back(fn.symbol());
    for (const Global &g : program.globals) defined.push_back(g.symbol);
    for (const StringLit &s : program.strings) defined.push_back(s.label);
    a_->predefine(defined);

    if (const Source *src = lineSource()) {
        const std::vector<std::string> &names = src->files();
        for (std::size_t i = 0; i < names.size(); i++)
            a_->fileEntry(static_cast<int>(i) + 1, names[i]);
    }

    emitData(program);
    finishChunk();
    for (const Function &fn : program.functions) bodies_[fn.symbol()] = &fn;
    if (inliner_) inliner_->summarize(program);
    for (const Function &fn : program.functions) emit(fn);
    if (!program.initFunction.empty()) {
        a_->initialiserEntry(program.initFunction, program.usesDsoHandle);
        finishChunk();
    }

    if (lineSource() != nullptr && writesDwarf()) {
        for (const Global &g : program.globals) {
            DwarfGlobal dg;
            dg.name = g.name;
            dg.symbol = g.symbol;
            dg.type = g.type;
            dg.external = !g.isStatic;
            dwarfGlobals_.push_back(dg);
        }
        writeDwarf(out_, kElfDwarf, target_, lineSource()->files().front(),
                   compDir(), dwarfFns_, dwarfGlobals_);
        finishChunk();
    }

    a_->preamble(sink_);
    for (const std::string &chunk : chunks_) sink_ << chunk;
    // After every function, so the .pdata the linker sees is in address order.
    sink_ << funcletPdata_;
    a_->postamble(sink_);
}
