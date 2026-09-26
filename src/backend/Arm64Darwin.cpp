#include "Arm64Darwin.h"

#include "../Source.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ostream>

int DarwinArm64Target::sizeOf(Kind k) const {
    switch (k) {
    case Kind::Void:                                       return 1;
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

    case Kind::LongDouble:                                 return 8;
    case Kind::Pointer: case Kind::NullPtr:                                    return 8;
    default:
        std::fprintf(stderr, "target: no size for this type yet\n");
        std::exit(1);
    }
}

int DarwinArm64Target::alignOf(Kind k) const { return sizeOf(k); }

static const char *const kArgRegs[] = { "x0", "x1", "x2", "x3",
                                        "x4", "x5", "x6", "x7" };
static const char *const kSseRegs[] = { "d0", "d1", "d2", "d3",
                                        "d4", "d5", "d6", "d7" };

// AAPCS64 as Apple writes it. `homogeneousFloatAggregates` is the float rule only
// this target has, and `aggregatesByReference` is deliberately **not** set: it is
// Microsoft's exact-size rule, and this target's is size <= 16 - see Abi.h.
static Abi aapcs64Apple() {
    Abi a;
    a.intRegs = kArgRegs;               a.intCount = 8;
    a.sseRegs = kSseRegs;               a.sseCount = 8;
    a.structReturnLimit = 16;
    a.scratch = "x9";                   a.scratch32 = "w9";
    a.homogeneousFloatAggregates = true;
    return a;
}

static const Abi kAapcs64AppleAbi = aapcs64Apple();

const Abi &Arm64DarwinBackend::abi() const { return kAapcs64AppleAbi; }

static const char *const kDarwinMacros[] = {
    "__aarch64__=1", "__arm64__=1", "__arm64=1",
    "__APPLE__=1", "__MACH__=1", "__LP64__=1", "_LP64=1", nullptr,
};
const char *const *Arm64DarwinBackend::identityMacros() const { return kDarwinMacros; }

std::unique_ptr<CodeGen> Arm64DarwinBackend::codegen(std::ostream &sink, Syntax) const {
    return std::unique_ptr<CodeGen>(new Arm64Darwin(sink, target_, kAapcs64AppleAbi));
}

static int alignTo(int n, int a) { return (n + a - 1) / a * a; }

void Arm64Darwin::unsupported(const char *what) {
    std::fprintf(stderr, "codegen: %s is not supported yet by the arm64-darwin "
                         "backend\n", what);
    std::exit(1);
}

std::string Arm64Darwin::label(const char *kind, int id) const {
    return labelPrefix_ + kind + "." + std::to_string(id);
}

std::string Arm64Darwin::userLabel(const std::string &name) const {
    return "L." + functionName_ + ".user." + name;
}

void Arm64Darwin::push() { out_ << "  str x0, [sp, #-16]!\n"; }
void Arm64Darwin::pop(const char *reg) {
    out_ << "  ldr " << reg << ", [sp], #16\n";
}

void Arm64Darwin::pushD() { out_ << "  str d0, [sp, #-16]!\n"; }
void Arm64Darwin::popD(const char *reg) {
    out_ << "  ldr " << reg << ", [sp], #16\n";
}

static std::string fpReg(const Type *t, int n) {
    return std::string(t->kind() == Kind::Float ? "s" : "d") + std::to_string(n);
}

void Arm64Darwin::movImm(const char *reg, long long value) {
    unsigned long long u = static_cast<unsigned long long>(value);
    out_ << "  mov " << reg << ", #" << (u & 0xffff) << "\n";
    for (int shift = 16; shift < 64; shift += 16) {
        unsigned long long part = (u >> shift) & 0xffff;
        if (part != 0) out_ << "  movk " << reg << ", #" << part
                            << ", lsl #" << shift << "\n";
    }
}

void Arm64Darwin::loadFpConst(const std::string &reg, const Type *t, double v) {
    if (t->kind() == Kind::Float) {
        float f = static_cast<float>(v);
        unsigned int bits;
        std::memcpy(&bits, &f, sizeof bits);
        movImm("x9", static_cast<long>(bits));
        out_ << "  fmov " << reg << ", w9\n";
    } else {
        unsigned long long bits;
        std::memcpy(&bits, &v, sizeof bits);
        movImm("x9", static_cast<long>(bits));
        out_ << "  fmov " << reg << ", x9\n";
    }
}

void Arm64Darwin::narrowInt(const Type *to) {
    int sz = to->size(target_);
    bool sign = to->isSigned(target_);
    if (sz == 1)      out_ << (sign ? "  sxtb x0, w0\n" : "  uxtb w0, w0\n");
    else if (sz == 2) out_ << (sign ? "  sxth x0, w0\n" : "  uxth w0, w0\n");
    else if (sz == 4) out_ << (sign ? "  sxtw x0, w0\n" : "  mov w0, w0\n");
}

Arm64Darwin::AggPlan Arm64Darwin::planFor(const Type *t) const {
    AggPlan p;
    // A class whose copy is a constructor call is never in registers: the caller
    // owns the storage and passes its address. Asked before the homogeneous-float
    // question, which would otherwise put a class of two floats in registers.
    if (t->nonTrivialCopy() || t->hasDestructor()) {
        p.byRef = true;
        p.words = 1;
        return p;
    }
    Kind elem;
    int n = homogeneousFloatCount(t, &elem);
    if (n > 0) { p.hfa = n; p.elem = elem; return p; }

    // **An empty class is ignored in the parameter list on this platform**, and
    // `sizeof` being 1 is not the question - measured with clang. Asked as "no
    // members and no vptr": dataSize is 0 on the types the compiler synthesises.
    if (t->isStructOrUnion() && t->members().empty() && !t->polymorphic()) {
        p.words = 0;
        return p;
    }

    int size = t->size(target_);
    if (size <= 16) { p.words = (size + 7) / 8; return p; }
    p.byRef = true;
    p.words = 1;
    return p;
}

// **The tail is composed, not approximated.** A lane of 3, 5, 6 or 7 live bytes
// took the largest store that fit and left the rest as the destination held it.
// The value register is dead after its own store, so the remainder shifts in place.
void Arm64Darwin::storeWord(const char *xreg, const char *base, int k, int size) {
    const int off = k * 8;
    const int left = size - off;
    const std::string w = std::string("w") + (xreg + 1);
    if (left >= 8) {
        out_ << "  str " << xreg << ", [" << base << ", #" << off << "]\n";
        return;
    }

    int done = 0, shifted = 0;
    auto bring = [&](int want) {
        if (want == shifted) return;
        out_ << "  lsr " << xreg << ", " << xreg << ", #" << (want - shifted) * 8
             << "\n";
        shifted = want;
    };
    if (left - done >= 4) {
        out_ << "  str " << w << ", [" << base << ", #" << (off + done) << "]\n";
        done += 4;
    }
    if (left - done >= 2) {
        bring(done);
        out_ << "  strh " << w << ", [" << base << ", #" << (off + done) << "]\n";
        done += 2;
    }
    if (left - done >= 1) {
        bring(done);
        out_ << "  strb " << w << ", [" << base << ", #" << (off + done) << "]\n";
    }
}

void Arm64Darwin::genAddr(const Expr &e) {
    if (const Var *v = dynamic_cast<const Var *>(&e)) {
        if (v->isLocal()) {

            movImm("x9", v->offset());
            out_ << "  sub x0, x29, x9\n";
        } else if (definedHere_.count(v->symbol()) != 0) {
            out_ << "  adrp x0, _" << v->symbol() << "@PAGE\n";
            out_ << "  add x0, x0, _" << v->symbol() << "@PAGEOFF\n";
        } else {

            out_ << "  adrp x0, _" << v->symbol() << "@GOTPAGE\n";
            out_ << "  ldr x0, [x0, _" << v->symbol() << "@GOTPAGEOFF]\n";
        }
        return;
    }
    if (const Unary *u = dynamic_cast<const Unary *>(&e)) {
        if (u->op() == '*') { u->operand().accept(*this); return; }
    }
    if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(&e)) {
        if (m->isBitField()) {
            std::fprintf(stderr,
                         "codegen: '%s' is a bit-field and has no address\n",
                         m->name().c_str());
            std::exit(1);
        }
        genAddr(m->object());
        addOffset(m->offset());
        return;
    }
    if (const StrLit *s = dynamic_cast<const StrLit *>(&e)) {
        out_ << "  adrp x0, " << s->label() << "@PAGE\n";
        out_ << "  add x0, x0, " << s->label() << "@PAGEOFF\n";
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
    unsupported("the address of this expression");
}

void Arm64Darwin::addOffset(int bytes) {
    if (bytes == 0) return;
    if (bytes > 0 && bytes < 4096) {
        out_ << "  add x0, x0, #" << bytes << "\n";
        return;
    }
    movImm("x9", bytes);
    out_ << "  add x0, x0, x9\n";
}

void Arm64Darwin::copyBlock(int size, const char *from, const char *to) {
    int off = 0;
    while (size - off >= 8) {
        out_ << "  ldr x10, [" << from << ", #" << off << "]\n";
        out_ << "  str x10, [" << to << ", #" << off << "]\n";
        off += 8;
    }
    while (size - off >= 4) {
        out_ << "  ldr w10, [" << from << ", #" << off << "]\n";
        out_ << "  str w10, [" << to << ", #" << off << "]\n";
        off += 4;
    }
    while (size - off >= 2) {
        out_ << "  ldrh w10, [" << from << ", #" << off << "]\n";
        out_ << "  strh w10, [" << to << ", #" << off << "]\n";
        off += 2;
    }
    while (size - off >= 1) {
        out_ << "  ldrb w10, [" << from << ", #" << off << "]\n";
        out_ << "  strb w10, [" << to << ", #" << off << "]\n";
        off += 1;
    }
}

void Arm64Darwin::bitFieldUnitAddr(const MemberAccess &m) {
    genAddr(m.object());
    addOffset(m.offset());
}

void Arm64Darwin::bitFieldExtract(const MemberAccess &m) {
    load(m.type());
    int left = 64 - m.bitOffset() - m.width();
    int right = 64 - m.width();
    if (left > 0) out_ << "  lsl x0, x0, #" << left << "\n";
    out_ << (m.type()->isSigned(target_) ? "  asr x0, x0, #" : "  lsr x0, x0, #")
         << right << "\n";
}

void Arm64Darwin::bitFieldInsert(const MemberAccess &m) {
    unsigned long long ones = (m.width() == 64) ? ~0ULL : ((1ULL << m.width()) - 1);
    unsigned long long mask = ones << m.bitOffset();

    movImm("x10", static_cast<long>(ones));
    out_ << "  and x10, x0, x10\n";
    if (m.bitOffset() != 0)
        out_ << "  lsl x10, x10, #" << m.bitOffset() << "\n";

    out_ << "  mov x11, x0\n";
    out_ << "  mov x0, x1\n";
    load(m.type());
    movImm("x9", static_cast<long>(~mask));
    out_ << "  and x0, x0, x9\n";
    out_ << "  orr x0, x0, x10\n";
    storeThrough(m.type(), "x1");

    out_ << "  mov x0, x11\n";
    int right = 64 - m.width();
    out_ << "  lsl x0, x0, #" << right << "\n";
    out_ << (m.type()->isSigned(target_) ? "  asr x0, x0, #" : "  lsr x0, x0, #")
         << right << "\n";
}

void Arm64Darwin::load(const Type *t) {
    if (t->isArray() || t->isStructOrUnion()) return;
    if (t->isFloating()) {
        out_ << "  ldr " << fpReg(t, 0) << ", [x0]\n";
        return;
    }

    int sz = t->size(target_);
    bool sign = t->isSigned(target_);
    if (sz == 1)      out_ << (sign ? "  ldrsb x0, [x0]\n" : "  ldrb w0, [x0]\n");
    else if (sz == 2) out_ << (sign ? "  ldrsh x0, [x0]\n" : "  ldrh w0, [x0]\n");
    else if (sz == 4) out_ << (sign ? "  ldrsw x0, [x0]\n" : "  ldr w0, [x0]\n");
    else              out_ << "  ldr x0, [x0]\n";
}

int Arm64Darwin::stackArgSlot(const Type *t, int &at) const {
    at = alignTo(at, t->align(target_));
    int here = at;
    at += t->size(target_);
    return here;
}

int Arm64Darwin::aggStackSlot(const Type *t, const AggPlan &p, int &at) const {
    if (p.byRef) {
        at = alignTo(at, 8);
        int here = at;
        at += 8;
        return here;
    }
    int a = t->align(target_);
    if (a < 8) a = 8;
    at = alignTo(at, a);
    int here = at;
    at += alignTo(t->size(target_), 8);
    return here;
}

// **The runtime arrives here with two values in registers**, and this is the
// whole of what the backend does about it: x0 the exception object and x1 the
// selector, into frame slots the parser knows. From then on they are locals.
void Arm64Darwin::landingPad(int pointerSlot, int selectorSlot) {
    out_ << "  mov x9, #" << pointerSlot << "\n";
    out_ << "  sub x9, x29, x9\n";
    out_ << "  str x0, [x9]\n";
    out_ << "  mov x9, #" << selectorSlot << "\n";
    out_ << "  sub x9, x29, x9\n";
    out_ << "  str w1, [x9]\n";
}
// Where a throw out of a cleanup pad lands: std::terminate, as clang's
// __clang_call_terminate does, jumped over on the way through.
std::string Arm64Darwin::terminatePad(int id) {
    out_ << "  b " << label("terminateskip", id) << "\n";
    out_ << label("terminate", id) << ":\n";
    out_ << "  bl __ZSt9terminatev\n";
    out_ << label("terminateskip", id) << ":\n";
    return label("terminate", id);
}

// **The language-specific data area, laid out exactly as clang lays it out** -
// every number read off clang's output, since the personality routine trusts
// the header.
static const Walker::LsdaSpelling kMachOLsda = {
    "L", ".section __TEXT,__gcc_except_tab", true, "_", "@GOT"
};

void Arm64Darwin::emitLsda(const std::string &symbol) {
    out_ << lsdaTable(kMachOLsda, symbol, lsdaTypes_);
}

void Arm64Darwin::storeToStack(const Type *t, int off) {
    if (t->isFloating()) {
        out_ << "  str " << fpReg(t, 0) << ", [sp, #" << off << "]\n";
        return;
    }
    switch (t->size(target_)) {
    case 1:  out_ << "  strb w0, [sp, #" << off << "]\n"; return;
    case 2:  out_ << "  strh w0, [sp, #" << off << "]\n"; return;
    case 4:  out_ << "  str w0, [sp, #" << off << "]\n"; return;
    default: out_ << "  str x0, [sp, #" << off << "]\n"; return;
    }
}

void Arm64Darwin::storeThrough(const Type *t, const char *addrReg) {
    if (t->isFloating()) {
        out_ << "  str " << fpReg(t, 0) << ", [" << addrReg << "]\n";
        return;
    }
    switch (t->size(target_)) {
    case 1:  out_ << "  strb w0, [" << addrReg << "]\n"; return;
    case 2:  out_ << "  strh w0, [" << addrReg << "]\n"; return;
    case 4:  out_ << "  str w0, [" << addrReg << "]\n"; return;
    default: out_ << "  str x0, [" << addrReg << "]\n"; return;
    }
}

void Arm64Darwin::visit(const Num &n) {
    if (n.type()->isFloating()) {
        loadFpConst(fpReg(n.type(), 0), n.type(), n.dvalue());
        return;
    }
    movImm("x0", n.value());
}

void Arm64Darwin::visit(const Var &n) { genAddr(n); load(n.type()); }

void Arm64Darwin::visit(const VaStart &n) {
    n.list().accept(*this);
    out_ << "  add x1, x29, #" << (16 + namedStackBytes_) << "\n";
    out_ << "  str x1, [x0]\n";
}

void Arm64Darwin::visit(const VaArg &n) {
    n.list().accept(*this);
    out_ << "  ldr x1, [x0]\n";
    out_ << "  add x2, x1, #8\n";
    out_ << "  str x2, [x0]\n";
    out_ << "  mov x0, x1\n";
    load(n.type());
}

void Arm64Darwin::visit(const StrLit &n) { genAddr(n); }

void Arm64Darwin::visit(const MemberAccess &n) {
    if (n.isBitField()) {
        bitFieldUnitAddr(n);
        bitFieldExtract(n);
        return;
    }
    genAddr(n);
    load(n.type());
}

void Arm64Darwin::visit(const Assign &n) {
    const MemberAccess *bf = dynamic_cast<const MemberAccess *>(&n.target());
    if (bf != nullptr && !bf->isBitField()) bf = nullptr;

    n.value().accept(*this);
    bool inFp = n.type()->isFloating();
    if (inFp) pushD(); else push();

    if (bf) bitFieldUnitAddr(*bf);
    else    genAddr(n.target());
    out_ << "  mov x1, x0\n";

    if (inFp) popD("d0"); else pop("x0");

    if (n.type()->isStructOrUnion()) {
        copyBlock(n.type()->size(target_), "x0", "x1");
        out_ << "  mov x0, x1\n";
        return;
    }
    if (bf) { bitFieldInsert(*bf); return; }

    storeThrough(n.type(), "x1");
}

void Arm64Darwin::visit(const Cast &n) {
    n.value().accept(*this);
    genConversion(n.value().type(), n.type());
}

void Arm64Darwin::genConversion(const Type *from, const Type *to) {
    if (to->isVoid()) return;

    bool fromF = from->isFloating(), toF = to->isFloating();

    if (fromF && toF) {

        if (fpReg(to, 0) != fpReg(from, 0))
            out_ << "  fcvt " << fpReg(to, 0) << ", " << fpReg(from, 0) << "\n";
        return;
    }
    if (!fromF && toF) {

        out_ << (from->isSigned(target_) ? "  scvtf " : "  ucvtf ")
             << fpReg(to, 0) << ", x0\n";
        return;
    }
    if (fromF && !toF) {
        out_ << (to->isSigned(target_) ? "  fcvtzs x0, " : "  fcvtzu x0, ")
             << fpReg(from, 0) << "\n";
        narrowInt(to);
        return;
    }
    narrowInt(to);
}

void Arm64Darwin::genTruth(const Expr &e) {
    e.accept(*this);
    if (e.type()->isFloating()) {
        out_ << "  fcmp " << fpReg(e.type(), 0) << ", #0.0\n";
        out_ << "  cset x0, ne\n";
        return;
    }
    out_ << "  cmp x0, #0\n";
    out_ << "  cset x0, ne\n";
}

void Arm64Darwin::visit(const Unary &n) {
    switch (n.op()) {
    case '-':
        n.operand().accept(*this);
        if (n.type()->isFloating()) {
            std::string r = fpReg(n.type(), 0);
            out_ << "  fneg " << r << ", " << r << "\n";
        } else {
            out_ << "  neg x0, x0\n";
        }
        return;
    case '~':
        n.operand().accept(*this);
        out_ << "  mvn x0, x0\n";
        return;
    case '!':
        n.operand().accept(*this);

        if (n.operand().type()->isFloating())
            out_ << "  fcmp " << fpReg(n.operand().type(), 0) << ", #0.0\n";
        else
            out_ << "  cmp x0, #0\n";
        out_ << "  cset x0, eq\n";
        return;
    case '&':
        genAddr(n.operand());
        return;
    case '*':
        n.operand().accept(*this);
        load(n.type());
        return;
    default:
        unsupported("this unary operator");
    }
}

void Arm64Darwin::visit(const Binary &n) {
    if (n.op() == BinOp::LAnd || n.op() == BinOp::LOr) {
        int id = nextLabel();
        bool isAnd = n.op() == BinOp::LAnd;
        genTruth(n.lhs());
        out_ << "  cmp x0, #0\n";
        out_ << (isAnd ? "  beq " : "  bne ") << label("shortcut", id) << "\n";
        genTruth(n.rhs());
        out_ << label("shortcut", id) << ":\n";
        return;
    }

    if (n.lhs().type()->isFloating() || n.rhs().type()->isFloating()) {
        const Type *ft = n.lhs().type();
        std::string a = fpReg(ft, 0), b = fpReg(ft, 1);

        n.lhs().accept(*this);
        pushD();
        n.rhs().accept(*this);
        out_ << "  fmov d1, d0\n";
        popD("d0");

        switch (n.op()) {
        case BinOp::Add: out_ << "  fadd " << a << ", " << a << ", " << b << "\n"; return;
        case BinOp::Sub: out_ << "  fsub " << a << ", " << a << ", " << b << "\n"; return;
        case BinOp::Mul: out_ << "  fmul " << a << ", " << a << ", " << b << "\n"; return;
        case BinOp::Div: out_ << "  fdiv " << a << ", " << a << ", " << b << "\n"; return;
        default: break;
        }

        const char *cond = nullptr;
        switch (n.op()) {
        case BinOp::Eq: cond = "eq"; break;
        case BinOp::Ne: cond = "ne"; break;
        case BinOp::Lt: cond = "mi"; break;
        case BinOp::Le: cond = "ls"; break;
        case BinOp::Gt: cond = "gt"; break;
        case BinOp::Ge: cond = "ge"; break;
        default: unsupported("this operator on floating point");
        }
        out_ << "  fcmp " << a << ", " << b << "\n";
        out_ << "  cset x0, " << cond << "\n";
        return;
    }

    n.lhs().accept(*this);
    push();
    n.rhs().accept(*this);
    out_ << "  mov x1, x0\n";
    pop("x0");

    bool sign = n.lhs().type()->isSigned(target_);

    switch (n.op()) {
    case BinOp::Add: out_ << "  add x0, x0, x1\n"; narrowInt(n.type()); return;
    case BinOp::Sub: out_ << "  sub x0, x0, x1\n"; narrowInt(n.type()); return;
    case BinOp::Mul: out_ << "  mul x0, x0, x1\n"; narrowInt(n.type()); return;
    case BinOp::Div:
        out_ << (sign ? "  sdiv x0, x0, x1\n" : "  udiv x0, x0, x1\n");
        narrowInt(n.type());
        return;
    case BinOp::Mod:
        out_ << (sign ? "  sdiv x2, x0, x1\n" : "  udiv x2, x0, x1\n");
        out_ << "  msub x0, x2, x1, x0\n";
        narrowInt(n.type());
        return;
    case BinOp::BitAnd: out_ << "  and x0, x0, x1\n"; narrowInt(n.type()); return;
    case BinOp::BitOr:  out_ << "  orr x0, x0, x1\n"; narrowInt(n.type()); return;
    case BinOp::BitXor: out_ << "  eor x0, x0, x1\n"; narrowInt(n.type()); return;
    case BinOp::Shl:    out_ << "  lsl x0, x0, x1\n"; narrowInt(n.type()); return;
    case BinOp::Shr:
        out_ << (sign ? "  asr x0, x0, x1\n" : "  lsr x0, x0, x1\n");
        narrowInt(n.type());
        return;
    default: break;
    }

    const char *cond = nullptr;
    switch (n.op()) {
    case BinOp::Eq: cond = "eq"; break;
    case BinOp::Ne: cond = "ne"; break;
    case BinOp::Lt: cond = sign ? "lt" : "lo"; break;
    case BinOp::Le: cond = sign ? "le" : "ls"; break;
    case BinOp::Gt: cond = sign ? "gt" : "hi"; break;
    case BinOp::Ge: cond = sign ? "ge" : "hs"; break;
    default: unsupported("this binary operator");
    }
    out_ << "  cmp x0, x1\n";
    out_ << "  cset x0, " << cond << "\n";
}

void Arm64Darwin::visit(const Postfix &n) {
    genAddr(n.target());
    push();
    load(n.type());

    if (n.type()->isFloating()) {
        std::string a = fpReg(n.type(), 0), b = fpReg(n.type(), 1);
        pushD();
        loadFpConst(b, n.type(), 1.0);
        out_ << (n.increment() ? "  fadd " : "  fsub ")
             << a << ", " << a << ", " << b << "\n";
        popD("d1");
        pop("x2");
        storeThrough(n.type(), "x2");
        out_ << "  fmov d0, d1\n";
        return;
    }

    push();
    movImm("x9", n.step());
    out_ << (n.increment() ? "  add x0, x0, x9\n" : "  sub x0, x0, x9\n");
    pop("x1");
    pop("x2");
    storeThrough(n.type(), "x2");
    out_ << "  mov x0, x1\n";
}

// **One walk of the parameter list, for the call site and the prologue alike.**
// Both made this decision by hand, and on x86 the same shape let the caller and
// the callee agree wrongly with nothing in the suite able to see it.
Arm64Darwin::Placement Arm64Darwin::placeArguments(
        const std::vector<const Type *> &types) const {
    Placement out;
    out.args.resize(types.size());
    int ints = 0, floats = 0, at = 0;
    for (std::size_t i = 0; i < types.size(); i++) {
        const Type *t = types[i];
        ArgPlace &p = out.args[i];
        if (t->isStructOrUnion()) {
            p.plan = planFor(t);
            // A homogeneous float aggregate takes float registers or none: once
            // it goes to the stack the rest of its kind follow it there, which
            // is what saturating the counter says.
            if (p.plan.hfa > 0) {
                if (floats + p.plan.hfa <= abi_.sseCount) {
                    p.inRegister = true;
                    p.firstReg = floats;
                    floats += p.plan.hfa;
                } else {
                    p.stackOffset = aggStackSlot(t, p.plan, at);
                    floats = abi_.sseCount;
                }
            } else {
                if (ints + p.plan.words <= abi_.intCount) {
                    p.inRegister = true;
                    p.firstReg = ints;
                    ints += p.plan.words;
                } else {
                    p.stackOffset = aggStackSlot(t, p.plan, at);
                    ints = abi_.intCount;
                }
            }
            continue;
        }
        if (t->isFloating()) {
            if (floats < abi_.sseCount) {
                p.inRegister = true;
                p.firstReg = floats++;
                continue;
            }
        } else if (ints < abi_.intCount) {
            p.inRegister = true;
            p.firstReg = ints++;
            continue;
        }
        p.stackOffset = stackArgSlot(t, at);
    }
    out.intsUsed = ints;
    out.floatsUsed = floats;
    out.stackBytes = at;
    return out;
}

void Arm64Darwin::visit(const Call &n) {
    const std::vector<ExprPtr> &args = n.args();
    std::size_t named = static_cast<std::size_t>(n.namedArgs());
    if (named > args.size()) named = args.size();
    std::size_t extra = args.size() - named;

    bool sret = n.type()->isStructOrUnion() &&
                planFor(n.type()).byRef;

    // The placement both ends of the call read. `dest` is only this side's
    // spelling of it: the register a scalar is popped into, and empty for
    // anything that travels on the stack or is a class.
    std::vector<const Type *> types;
    types.reserve(named);
    for (std::size_t i = 0; i < named; i++) types.push_back(args[i]->type());
    const Placement plan = placeArguments(types);

    std::vector<std::string> dest;
    dest.reserve(named);
    for (std::size_t i = 0; i < named; i++) {
        const Type *t = args[i]->type();
        const ArgPlace &p = plan.args[i];
        if (t->isStructOrUnion() || !p.inRegister) dest.push_back("");
        else if (t->isFloating())                  dest.push_back(fpReg(t, p.firstReg));
        else                                       dest.push_back(abi_.intRegs[p.firstReg]);
    }

    int variadicBase = alignTo(plan.stackBytes, 8);
    int extraBytes = alignTo(variadicBase + static_cast<int>(extra) * 8, 16);
    if (extraBytes > 0) {
        movImm("x9", extraBytes);
        out_ << "  sub sp, sp, x9\n";
    }
    for (std::size_t i = 0; i < named; i++) {
        if (plan.args[i].stackOffset < 0) continue;

        if (args[i]->type()->isStructOrUnion()) continue;
        args[i]->accept(*this);
        storeToStack(args[i]->type(), plan.args[i].stackOffset);
    }
    for (std::size_t k = 0; k < extra; k++) {
        const ExprPtr &a = args[named + k];

        if (a->type()->isStructOrUnion())
            unsupported("a struct passed through '...'");
        a->accept(*this);
        if (a->type()->isFloating())
            out_ << "  str d0, [sp, #" << (variadicBase + k * 8) << "]\n";
        else
            out_ << "  str x0, [sp, #" << (variadicBase + k * 8) << "]\n";
    }

    if (n.callee() != nullptr) {
        n.callee()->accept(*this);
        push();
    }

    for (std::size_t i = 0; i < named; i++) {
        if (!args[i]->type()->isStructOrUnion()) continue;
        args[i]->accept(*this);
        movImm("x9", n.argSlot(i));
        out_ << "  sub x1, x29, x9\n";
        copyBlock(args[i]->type()->size(target_), "x0", "x1");
    }

    for (std::size_t i = 0; i < named; i++) {
        if (dest[i].empty()) continue;
        args[i]->accept(*this);
        if (args[i]->type()->isFloating()) pushD(); else push();
    }
    for (std::size_t i = named; i-- > 0; ) {
        if (dest[i].empty()) continue;
        if (args[i]->type()->isFloating()) popD(dest[i].c_str());
        else                               pop(dest[i].c_str());
    }

    for (std::size_t i = 0; i < named; i++) {
        const Type *t = args[i]->type();
        if (!t->isStructOrUnion()) continue;
        movImm("x9", n.argSlot(i));
        out_ << "  sub x9, x29, x9\n";
        const AggPlan &p = plan.args[i].plan;

        if (plan.args[i].stackOffset >= 0) {
            if (p.byRef) {
                out_ << "  str x9, [sp, #" << plan.args[i].stackOffset << "]\n";
            } else {
                out_ << "  add x11, sp, #" << plan.args[i].stackOffset << "\n";
                copyBlock(args[i]->type()->size(target_), "x9", "x11");
            }
            continue;
        }

        if (p.byRef) {
            out_ << "  mov " << abi_.intRegs[plan.args[i].firstReg] << ", x9\n";
        } else if (p.hfa > 0) {
            const char *w = (p.elem == Kind::Float) ? "s" : "d";
            int step = (p.elem == Kind::Float) ? 4 : 8;
            for (int k = 0; k < p.hfa; k++)
                out_ << "  ldr " << w << (plan.args[i].firstReg + k)
                     << ", [x9, #" << (k * step) << "]\n";
        } else {
            for (int k = 0; k < p.words; k++)
                out_ << "  ldr " << abi_.intRegs[plan.args[i].firstReg + k]
                     << ", [x9, #" << (k * 8) << "]\n";
        }
    }

    if (sret) {
        movImm("x9", n.resultSlot());
        out_ << "  sub x8, x29, x9\n";
    }

    if (n.callee() != nullptr) {

        pop("x16");
        out_ << "  blr x16\n";
    } else {
        out_ << "  bl _" << n.symbol() << "\n";
    }
    if (extraBytes > 0) {
        movImm("x9", extraBytes);
        out_ << "  add sp, sp, x9\n";
    }
    if (n.type()->isStructOrUnion()) {
        movImm("x9", n.resultSlot());
        out_ << "  sub x9, x29, x9\n";
        AggPlan p = planFor(n.type());
        if (p.byRef) {
        } else if (p.hfa > 0) {
            const char *w = (p.elem == Kind::Float) ? "s" : "d";
            int step = (p.elem == Kind::Float) ? 4 : 8;
            for (int k = 0; k < p.hfa; k++)
                out_ << "  str " << w << k << ", [x9, #" << (k * step) << "]\n";
        } else {
            static const char *const ret[2] = { "x0", "x1" };
            for (int k = 0; k < p.words; k++)
                storeWord(ret[k], "x9", k, n.type()->size(target_));
        }
        out_ << "  mov x0, x9\n";
        return;
    }

    if (!n.type()->isVoid() && !n.type()->isFloating())
        narrowInt(n.type());
}

void Arm64Darwin::defineLabel(const std::string &l) { out_ << l << ":\n"; }
void Arm64Darwin::jump(const std::string &l) { out_ << "  b " << l << "\n"; }
void Arm64Darwin::branchIfZero(const std::string &l) {
    out_ << "  cmp x0, #0\n";
    out_ << "  beq " << l << "\n";
}
void Arm64Darwin::branchIfNotZero(const std::string &l) {
    out_ << "  cmp x0, #0\n";
    out_ << "  bne " << l << "\n";
}

void Arm64Darwin::caseBranch(long long v, const std::string &l) {
    if (v >= 0 && v < 4096) {
        out_ << "  cmp x0, #" << v << "\n";
    } else {
        movImm("x9", v);
        out_ << "  cmp x0, x9\n";
    }
    out_ << "  beq " << l << "\n";
}

void Arm64Darwin::visit(const Return &n) {
    markLine(n);
    if (!n.hasValue()) { out_ << "  b " << returnLabel_ << "\n"; return; }
    n.value().accept(*this);

    const Type *t = n.value().type();
    if (t->isStructOrUnion()) {
        AggPlan p = planFor(t);
        if (p.byRef) {
            movImm("x9", sretSlot_);
            out_ << "  sub x9, x29, x9\n";
            out_ << "  ldr x1, [x9]\n";
            copyBlock(t->size(target_), "x0", "x1");
            out_ << "  mov x0, x1\n";
        } else if (p.hfa > 0) {
            const char *w = (p.elem == Kind::Float) ? "s" : "d";
            int step = (p.elem == Kind::Float) ? 4 : 8;
            for (int k = 0; k < p.hfa; k++)
                out_ << "  ldr " << w << k << ", [x0, #" << (k * step) << "]\n";
        } else {

            static const char *const ret[2] = { "x0", "x1" };
            for (int k = p.words; k-- > 0; )
                out_ << "  ldr " << ret[k] << ", [x0, #" << (k * 8) << "]\n";
        }
    }
    out_ << "  b " << returnLabel_ << "\n";
}

static int p2AlignOf(int bytes) {
    int p = 0;
    while ((1 << p) < bytes) p++;
    return p;
}

void Arm64Darwin::emitGlobal(const Global &g, Segment seg) {
    int size = g.type->size(target_);
    int p2 = p2AlignOf(g.align > objectAlign(g.type, target_) ? g.align : objectAlign(g.type, target_));
    if (!g.isStatic) out_ << "  .globl _" << g.symbol << "\n";
    // A writable weak object is one object across the program - a template's
    // static member - so it may not be hidden per unit; clang writes
    // `.weak_definition` for it, in __data with `.space` rather than zerofill.
    const bool writable = seg == Segment::Data || seg == Segment::Bss;
    if (g.isInline)
        out_ << (writable ? "  .weak_definition _" : "  .weak_def_can_be_hidden _")
             << g.symbol << "\n";

    if (seg == Segment::Bss && g.isInline) {
        out_ << "  .section __DATA,__data\n  .p2align " << p2 << "\n_"
             << g.symbol << ":\n  .space " << size << "\n";
        return;
    }
    if (seg == Segment::Bss) {
        out_ << "  .zerofill __DATA,__bss,_" << g.symbol << ","
             << size << "," << p2 << "\n";
        return;
    }

    out_ << "  .p2align " << p2 << "\n";
    out_ << "_" << g.symbol << ":\n";
    int at = 0;
    for (const GlobalPiece &p : g.init) {
        if (p.offset > at) out_ << "  .space " << (p.offset - at) << "\n";

        if (!p.symbol.empty()) {
            out_ << "  .quad " << (p.symbol[0] == '.' ? "" : "_") << p.symbol;
            if (p.value > 0) out_ << "+" << p.value;
            else if (p.value < 0) out_ << "-" << -p.value;
            out_ << "\n";
            at = p.offset + p.size;
            continue;
        }

        switch (p.size) {
        case 1:  out_ << "  .byte " << p.value << "\n"; break;
        case 2:  out_ << "  .short " << p.value << "\n"; break;
        case 4:  out_ << "  .long " << p.value << "\n"; break;
        default: out_ << "  .quad " << p.value << "\n"; break;
        }
        at = p.offset + p.size;
    }
    if (at < size) out_ << "  .space " << (size - at) << "\n";
}

void Arm64Darwin::emitData(const Program &program) {

    for (int pass = 0; pass < 2; pass++) {
        bool wantWide = (pass == 1);
        bool opened = false;
        for (const StringLit &s : program.strings) {
            if ((s.width > 1) != wantWide) continue;
            if (!opened) {
                out_ << (wantWide ? "  .section __TEXT,__const\n"
                                  : "  .section __TEXT,__cstring,cstring_literals\n");
                opened = true;
            }
            if (s.width > 1) {
                int p2 = 0;
                while ((1 << p2) < s.width) p2++;
                out_ << "  .p2align " << p2 << "\n";
            }
            out_ << s.label << ":\n";
            out_ << "  .ascii \"";
            for (unsigned char c : s.bytes) {
                if (c == '"' || c == '\\') out_ << '\\' << c;
                else if (c == '\n')        out_ << "\\n";
                else if (c == '\t')        out_ << "\\t";
                else if (c >= 32 && c < 127) out_ << c;
                else out_ << '\\' << static_cast<char>('0' + ((c >> 6) & 7))
                          << static_cast<char>('0' + ((c >> 3) & 7))
                          << static_cast<char>('0' + (c & 7));
            }
            out_ << "\"\n";
        }
    }

    struct Bucket { Segment seg; const char *open; };
    const Bucket order[] = {
        { Segment::Const, "  .section __TEXT,__const\n" },

        { Segment::ConstRelocated, "  .section __DATA,__const\n" },
        { Segment::Data,  "  .section __DATA,__data\n" },
        { Segment::Bss,   nullptr },
    };
    for (const Bucket &b : order) {
        bool opened = false;
        for (const Global &g : program.globals) {
            if (segmentFor(g) != b.seg) continue;
            if (!opened && b.open != nullptr) { out_ << b.open; }
            opened = true;
            emitGlobal(g, b.seg);
        }
    }
}

void Arm64Darwin::emitFunction(const Function &fn) {
    resetLabels();
    // Labels are built from the symbol rather than the name: two overloads
    // share a name and must not share a label.
    functionName_ = fn.symbol();
    labelPrefix_ = "L." + fn.symbol() + ".";
    returnLabel_ = "L.return." + fn.symbol();

    out_ << "  .section __TEXT,__text,regular,pure_instructions\n";
    if (!fn.isStatic()) out_ << "  .globl _" << fn.symbol() << "\n";
    // **An inline definition may appear in several translation units**, so the
    // linker folds the copies rather than rejecting them. Mach-O's spelling,
    // measured from clang, keeps the `.globl` beside it.
    if (fn.isInline())
        out_ << "  .weak_def_can_be_hidden _" << fn.symbol() << "\n";
    out_ << "  .p2align 2\n";
    out_ << "_" << fn.symbol() << ":\n";
    // A second name for the same code - see Function::alias. It is a label at
    // the same address rather than a copy of the body.
    if (!fn.alias().empty()) {
        if (!fn.isStatic()) out_ << "  .globl _" << fn.alias() << "\n";
        if (fn.isInline())
            out_ << "  .weak_def_can_be_hidden _" << fn.alias() << "\n";
        out_ << "_" << fn.alias() << ":\n";
    }
    if (const Source *src = lineSource()) {
        Source::Place at = src->locate(fn.pos());
        DwarfFunction d;
        d.name = fn.name();
        d.begin = "Lfunc.begin." + fn.symbol();
        d.end = "Lfunc.end." + fn.symbol();
        d.file = at.file + 1;
        d.line = at.line;
        d.external = !fn.isStatic();
        d.returns = fn.returns();
        d.locals = &fn.locals();
        dwarfFns_.push_back(d);
        resetBlocks(fn.blocks());
        out_ << d.begin << ":\n";
    } else if (fn.hasLandingPads()) {
        // The call-site table measures from here, so the label has to exist
        // whether or not there is debug information.
        out_ << "Lfunc.begin." << fn.symbol() << ":\n";
    }
    clearCallSites();

    markLine(fn.pos());
    // **Unwind data, and it is the same three lines in every function here.**
    out_ << "  .cfi_startproc\n";
    // **A function with a landing pad names its personality and its table here**,
    // before anything else: the unwinder finds both through the CFI, and the
    // table itself is written after the body, once its labels exist.
    if (fn.hasLandingPads()) {
        out_ << "  .cfi_personality 155, ___gxx_personality_v0\n";
        out_ << "  .cfi_lsda 16, Lexception." << fn.symbol() << "\n";
    }
    out_ << "  stp x29, x30, [sp, #-16]!\n";
    out_ << "  mov x29, sp\n";
    out_ << "  .cfi_def_cfa w29, 16\n";
    out_ << "  .cfi_offset w30, -8\n";
    out_ << "  .cfi_offset w29, -16\n";

    int frame = alignTo(fn.frameSize(), 16);
    if (frame > 0) {
        movImm("x9", frame);
        out_ << "  sub sp, sp, x9\n";
    }

    sretSlot_ = fn.sretSlot();
    if (sretSlot_ != 0) {
        movImm("x9", sretSlot_);
        out_ << "  sub x9, x29, x9\n";
        out_ << "  str x8, [x9]\n";
    }

    // **The same walk the call site made, made once.** placeArguments answers
    // where each parameter is; the base is this side's, an incoming stack
    // argument sitting above the saved frame pointer and return address.
    const std::vector<Param> &ps = fn.params();
    std::vector<const Type *> ptypes;
    ptypes.reserve(ps.size());
    for (std::size_t i = 0; i < ps.size(); i++) ptypes.push_back(ps[i].type);
    const Placement plan = placeArguments(ptypes);

    for (std::size_t i = 0; i < ps.size(); i++) {
        const ArgPlace &pl = plan.args[i];
        if (ps[i].type->isStructOrUnion()) {
            const AggPlan &p = pl.plan;
            const bool inRegister = pl.inRegister;
            const int from = pl.stackOffset;

            movImm("x9", ps[i].offset);
            out_ << "  sub x9, x29, x9\n";

            if (!inRegister) {
                out_ << "  mov x11, x9\n";
                out_ << "  add x9, x29, #" << (16 + from) << "\n";
                if (p.byRef) out_ << "  ldr x9, [x9]\n";
                copyBlock(ps[i].type->size(target_), "x9", "x11");
                continue;
            }

            if (p.byRef) {
                out_ << "  mov x11, x9\n";
                copyBlock(ps[i].type->size(target_), abi_.intRegs[pl.firstReg], "x11");
            } else if (p.hfa > 0) {
                const char *w = (p.elem == Kind::Float) ? "s" : "d";
                int step = (p.elem == Kind::Float) ? 4 : 8;
                for (int k = 0; k < p.hfa; k++)
                    out_ << "  str " << w << (pl.firstReg + k)
                         << ", [x9, #" << (k * step) << "]\n";
            } else {
                for (int k = 0; k < p.words; k++)
                    storeWord(abi_.intRegs[pl.firstReg + k], "x9", k,
                              ps[i].type->size(target_));
            }
            continue;
        }
        movImm("x9", ps[i].offset);
        out_ << "  sub x9, x29, x9\n";

        if (!pl.inRegister) {
            out_ << "  mov x11, x9\n";
            out_ << "  add x9, x29, #" << (16 + pl.stackOffset) << "\n";
            copyBlock(ps[i].type->size(target_), "x9", "x11");
            continue;
        }

        if (ps[i].type->isFloating()) {
            out_ << "  str " << fpReg(ps[i].type, pl.firstReg) << ", [x9]\n";
        } else {
            out_ << "  mov x0, " << abi_.intRegs[pl.firstReg] << "\n";
            storeThrough(ps[i].type, "x9");
        }
    }

    namedStackBytes_ = alignTo(plan.stackBytes, 8);

    fn.body().accept(*this);

    out_ << "  mov x0, #0\n";
    out_ << returnLabel_ << ":\n";
    out_ << "  mov sp, x29\n";
    out_ << "  ldp x29, x30, [sp], #16\n";
    out_ << "  ret\n";
    // The end label goes before .cfi_endproc and before the table, because
    // the last call-site range measures up to it.
    if (lineSource() || !callSites().empty())
        out_ << "Lfunc.end." << fn.symbol() << ":\n";
    out_ << "  .cfi_endproc\n";
    if (!callSites().empty()) {
        emitLsda(fn.symbol());
        out_ << "  .section __TEXT,__text,regular,pure_instructions\n";
    }
    if (lineSource()) dwarfFns_.back().blocks = blocks();
}

void Arm64Darwin::emitLoc(int file, int line, int column) {
    out_ << "  .loc " << file << " " << line << " " << column << "\n";
}

void Arm64Darwin::run(const Program &program) {
    definedHere_.clear();
    for (const Global &g : program.globals)   definedHere_.insert(g.symbol);
    for (const Function &f : program.functions) definedHere_.insert(f.symbol());

    if (const Source *src = lineSource()) {
        const std::vector<std::string> &names = src->files();
        for (std::size_t i = 0; i < names.size(); i++)
            out_ << "  .file " << (i + 1) << " \"" << names[i] << "\"\n";
    }

    emitData(program);
    for (const Function &fn : program.functions) emitFunction(fn);
    // Measured from clang: the init function's address in __mod_init_func.
    if (!program.initFunction.empty())
        out_ << "  .section __DATA,__mod_init_func,mod_init_funcs\n"
                "  .p2align 3, 0x0\n  .quad _" << program.initFunction << "\n";
    if (const Source *src = lineSource()) {
        for (const Global &g : program.globals) {
            DwarfGlobal dg;
            dg.name = g.name;
            dg.symbol = g.symbol;
            dg.type = g.type;
            dg.external = !g.isStatic;
            dwarfGlobals_.push_back(dg);
        }
        std::string dwarf;
        writeDwarf(dwarf, kMachODwarf, target_, src->files().front(), compDir(),
                   dwarfFns_, dwarfGlobals_);
        out_ << dwarf;
    }
    out_ << ".subsections_via_symbols\n";
    sink_ << out_.str();
}
