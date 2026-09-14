// The parser: initialisers. Braced initialisers flattened onto an object's
// bytes, the run-time stores a local needs against the static image a global
// gets, and the integer, floating and address folding that chooses between them.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <cctype>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstring>

Parser::Init Parser::parseInitialiser() {
    Init in;
    in.pos = peek().pos;
    if (consume("{")) {
        in.isList = true;
        // **`{}` is not an empty list, it is value-initialisation.** [dcl.init]/11
        // sends it to /8, the paragraph `T()` already follows here; every reader
        // of an `Init` below takes no items as "zero it", so none tests for this.
        if (consume("}")) return in;
        for (;;) {
            in.items.push_back(parseInitialiser());
            if (consume("}")) break;
            expect(",");
            if (consume("}")) break;
        }
        return in;
    }
    in.value = assign();
    return in;
}

bool Parser::atBracedInitialiser(const std::string &name) {
    if (!peek().is("{")) return false;
    if (!peekAt(1).is("}"))
        src_.fail(peek().pos, "'" + name + "{...}' is list-initialisation, and "
                              "that is not supported yet - write '" + name +
                              " = {...}', which for everything this compiler "
                              "reads braces on means the same thing. Empty "
                              "braces, '" + name + "{}', are read: they "
                              "value-initialise");
    return true;
}

const StrLit *Parser::stringInitialiser(const Init &in, const Type *type) {
    if (in.isList || !type->isArray()) return nullptr;
    const StrLit *s = dynamic_cast<const StrLit *>(in.value.get());
    if (s == nullptr) return nullptr;

    Kind want = type->pointee()->kind();
    Kind have = s->type()->pointee()->kind();
    bool wantNarrow = (want == Kind::Char || want == Kind::SChar || want == Kind::UChar);
    bool haveNarrow = (have == Kind::Char || have == Kind::SChar || have == Kind::UChar);
    if (wantNarrow != haveNarrow) return nullptr;
    if (!wantNarrow && want != have) return nullptr;
    return s;
}

void Parser::skipInit(const Type *type, InitCursor &c) {
    if (c.done()) return;
    Init &item = c.cur();

    if (item.isList)                        { c.at++; return; }
    if (stringInitialiser(item, type))      { c.at++; return; }

    if (type->isArray()) {
        const Type *elem = type->pointee();
        for (long long i = 0; i < type->length() && !c.done(); i++) skipInit(elem, c);
        return;
    }
    if (type->isStructOrUnion()) {
        const std::vector<Member> &members = type->members();
        std::size_t count = type->kind() == Kind::Union
                          ? (members.empty() ? std::size_t(0) : std::size_t(1))
                          : members.size();
        for (std::size_t i = 0; i < count && !c.done(); i++) {
            if (members[i].name.empty()) continue;
            skipInit(members[i].type, c);
        }
        return;
    }
    c.at++;
}

long long Parser::inferredLength(const Init &in, const Type *element, std::size_t pos) {
    if (const StrLit *s = stringInitialiser(in, types_.arrayOf(element, 1)))
        return static_cast<long long>(s->text().size()) + 1;
    if (!in.isList)
        src_.fail(pos, "an array with no length needs a braced initialiser to "
                       "count, or a string to measure");
    // `{}` counts nothing, and an array of no elements is not C++ - clang says
    // the same, calling it an extension it will not take under -pedantic-errors.
    if (in.items.empty())
        src_.fail(pos, "'{}' gives an array with no length nothing to count "
                       "from, and an array of no elements is not C++ - write a "
                       "length, or a value to take one from");

    if (element->isArray() || element->isStructOrUnion()) {
        InitCursor c{ const_cast<std::vector<Init> *>(&in.items), 0 };
        long long rows = 0;
        while (!c.done()) {
            std::size_t before = c.at;
            skipInit(element, c);
            if (c.at == before) break;
            rows++;
        }
        return rows;
    }
    return static_cast<long long>(in.items.size());
}

ExprPtr Parser::targetFor(const std::string &name,
                          const std::vector<InitStep> &path) {
    ExprPtr e = objectRef(name);
    for (const InitStep &s : path) {
        if (s.member != nullptr) {
            const Member *m = s.member;
            ExprPtr acc(new MemberAccess(std::move(e), m->name, m->offset,
                                         m->width, m->bitOffset));
            acc->setType(m->type);
            e = std::move(acc);
        } else {
            const Type *elem = e->type()->pointee();
            ExprPtr index(new Num(s.index));
            index->setType(types_.intType());
            ExprPtr sum = pointerAdd(decay(std::move(e)), std::move(index));
            ExprPtr deref(new Unary('*', std::move(sum)));
            deref->setType(elem);
            e = std::move(deref);
        }
    }
    return e;
}

void Parser::initStore(const std::string &name, std::vector<InitStep> &path,
                       ExprPtr value, std::size_t pos,
                       std::vector<StmtPtr> &out) {
    ExprPtr target = targetFor(name, path);
    const Type *to = target->type();
    checkAssignable(*value, to, pos, "'" + name + "'");
    ExprPtr a(new Assign(std::move(target), convert(std::move(value), to)));
    a->setType(to);
    out.push_back(StmtPtr(new ExprStmt(std::move(a))));
}

void Parser::initZero(const std::string &name, std::vector<InitStep> &path,
                      const Type *type, std::size_t pos,
                      std::vector<StmtPtr> &out) {
    if (type->isArray()) {
        const Type *elem = type->pointee();
        for (long long i = 0; i < type->length(); i++) {
            path.push_back(InitStep{ nullptr, i });
            initZero(name, path, elem, pos, out);
            path.pop_back();
        }
        return;
    }
    if (type->isStructOrUnion()) {
        const std::vector<Member> &members = type->members();
        std::size_t count = type->kind() == Kind::Union
                          ? (members.empty() ? std::size_t(0) : std::size_t(1))
                          : members.size();
        for (std::size_t i = 0; i < count; i++) {
            if (members[i].name.empty()) continue;
            path.push_back(InitStep{ &members[i], 0 });
            initZero(name, path, members[i].type, pos, out);
            path.pop_back();
        }
        return;
    }
    ExprPtr z;
    if (type->isFloating()) { z.reset(new Num(0.0L)); z->setType(types_.doubleType()); }
    else                    { z.reset(new Num(0LL));  z->setType(types_.intType()); }
    initStore(name, path, std::move(z), pos, out);
}

void Parser::emitString(const std::string &name, std::vector<InitStep> &path,
                        const Type *type, const StrLit *s, std::size_t pos,
                        std::vector<StmtPtr> &out) {
    long long len = type->length();
    const std::string &text = s->text();
    if (static_cast<long long>(text.size()) > len)
        src_.fail(pos, "'" + name + "' holds " + std::to_string(len) +
                       " characters and the string has " +
                       std::to_string(text.size()));
    for (long long i = 0; i < len; i++) {
        path.push_back(InitStep{ nullptr, i });
        long long ch = i < static_cast<long long>(text.size())
                ? static_cast<long long>(static_cast<unsigned char>(
                      text[static_cast<std::size_t>(i)]))
                : 0L;
        ExprPtr c(new Num(ch));
        c->setType(types_.intType());
        initStore(name, path, std::move(c), pos, out);
        path.pop_back();
    }
}

void Parser::emitFill(const std::string &name, std::vector<InitStep> &path,
                      const Type *type, InitCursor &c,
                      std::vector<StmtPtr> &out) {
    if (c.done()) return;
    Init &item = c.cur();

    if (item.isList) {
        c.at++;
        emitInit(name, path, type, item, out);
        return;
    }
    if (const StrLit *s = stringInitialiser(item, type)) {
        c.at++;
        emitString(name, path, type, s, item.pos, out);
        return;
    }
    if (type->isArray() || type->isStructOrUnion()) {
        emitAggregate(name, path, type, c, item.pos, out);
        return;
    }

    c.at++;
    checkNarrowing(type, *item.value, item.pos, "'" + name + "'");
    initStore(name, path, decay(std::move(item.value)), item.pos, out);
}

void Parser::emitAggregate(const std::string &name, std::vector<InitStep> &path,
                           const Type *type, InitCursor &c, std::size_t pos,
                           std::vector<StmtPtr> &out) {
    if (type->isArray()) {
        const Type *elem = type->pointee();
        for (long long i = 0; i < type->length(); i++) {
            path.push_back(InitStep{ nullptr, i });
            if (c.done()) initZero(name, path, elem, pos, out);
            else          emitFill(name, path, elem, c, out);
            path.pop_back();
        }
        return;
    }
    const std::vector<Member> &members = type->members();
    std::size_t count = type->kind() == Kind::Union
                      ? (members.empty() ? std::size_t(0) : std::size_t(1))
                      : members.size();
    for (std::size_t i = 0; i < count; i++) {
        if (members[i].name.empty()) continue;
        path.push_back(InitStep{ &members[i], 0 });
        if (c.done()) initZero(name, path, members[i].type, pos, out);
        else          emitFill(name, path, members[i].type, c, out);
        path.pop_back();
    }
}

void Parser::emitInit(const std::string &name, std::vector<InitStep> &path,
                      const Type *type, Init &in, std::vector<StmtPtr> &out) {
    // Value-initialisation, which for everything that reaches here is zeroing:
    // a class with a constructor to run never does, the declaration parser
    // takes that shape before an `Init` is built for it.
    if (in.isList && in.items.empty()) {
        initZero(name, path, type, in.pos, out);
        return;
    }
    if (const StrLit *s = stringInitialiser(in, type)) {
        emitString(name, path, type, s, in.pos, out);
        return;
    }

    if (type->isArray()) {
        if (!in.isList)
            src_.fail(in.pos, "'" + name + "' is an array and needs a braced "
                              "initialiser");
    } else if (type->isStructOrUnion()) {

        if (!in.isList) {
            initStore(name, path, decay(std::move(in.value)), in.pos, out);
            return;
        }
    } else {
        if (!in.isList) {
            initStore(name, path, decay(std::move(in.value)), in.pos, out);
            return;
        }
        if (in.items.size() != 1)
            src_.fail(in.pos, "'" + name + "' is not an aggregate and takes one "
                              "value");
        if (!in.items[0].isList)
            checkNarrowing(type, *in.items[0].value, in.items[0].pos,
                           "'" + name + "'");
        emitInit(name, path, type, in.items[0], out);
        return;
    }

    InitCursor c{ &in.items, 0 };
    emitAggregate(name, path, type, c, in.pos, out);
    if (!c.done())
        src_.fail(c.cur().pos, "'" + name + "' is full, and there are " +
                               std::to_string(in.items.size() - c.at) +
                               " more initialiser(s) after this one");
}

static long double inType(const Type *t, const Target &target, long double v) {
    if (t->kind() == Kind::Float) return static_cast<float>(v);
    if (t->kind() == Kind::Double ||
        (t->kind() == Kind::LongDouble && !t->isX87(target)))
        return static_cast<double>(v);
    return v;
}

// **Every value below is carried as a `double`, exactly, or the fold says so.**
// A host `long double` is 64 bits of significand on the Linux box and 53 on the
// Mac, so the fold works in double and flags `past53` and `x87Rounded`.
bool Parser::foldDouble(const Expr &e, const Target &target, long double *out,
                        bool *past53, bool *x87Rounded) const {
    // **A const floating object read back**, the twin of the integral read-back fold() already
    // makes: `const double b = (1 - f) * a;` is dynamic initialisation by the letter of
    // [basic.start.init], and folding it is what every compiler does instead.
    if (const Var *v = dynamic_cast<const Var *>(&e)) {
        if (v->isLocal()) {
            if (const Local *l = findLocal(v->name()))
                if (l->isConstantDouble) { *out = l->constantDouble; return true; }
            return false;
        }
        if (const GlobalSym *g = findGlobal(v->name()))
            if (g->isConstantDouble) { *out = g->constantDouble; return true; }
        // As in fold(): a static member lives under its symbol, not in globals_.
        if (const StaticConst *sc = findStaticConst(v->symbol()))
            if (sc->dknown) { *out = sc->dvalue; return true; }
        return false;
    }
    if (const Num *n = dynamic_cast<const Num *>(&e)) {
        if (n->type()->isFloating()) {
            *out = inType(n->type(), target, n->dvalue());
        } else {
            unsigned long long a = n->value() < 0
                ? 0ULL - static_cast<unsigned long long>(n->value())
                : static_cast<unsigned long long>(n->value());
            while (a != 0 && (a & 1) == 0) a >>= 1;
            if (a >= (1ULL << 53)) *past53 = true;
            *out = static_cast<double>(n->value());
        }
        return true;
    }
    if (const Cast *c = dynamic_cast<const Cast *>(&e)) {
        if (!foldDouble(c->value(), target, out, past53, x87Rounded))
            return false;

        const Type *ct = c->type();
        if (ct->kind() == Kind::Float)       *out = static_cast<float>(*out);
        else if (ct->kind() == Kind::Double) *out = static_cast<double>(*out);
        else if (ct->kind() == Kind::LongDouble && !ct->isX87(target))
                                             *out = static_cast<double>(*out);
        else if (!ct->isFloating()) {
            if (ct->isSigned(target))
                *out = static_cast<long double>(
                           static_cast<long long>(*out));
            else
                *out = static_cast<long double>(
                           static_cast<unsigned long long>(*out));
        }
        return true;
    }
    if (const Unary *u = dynamic_cast<const Unary *>(&e)) {
        if (u->op() == '-' &&
            foldDouble(u->operand(), target, out, past53, x87Rounded)) {
            *out = -*out;
            return true;
        }
    }

    if (const Binary *b = dynamic_cast<const Binary *>(&e)) {
        long double l, r;
        if (!foldDouble(b->lhs(), target, &l, past53, x87Rounded) ||
            !foldDouble(b->rhs(), target, &r, past53, x87Rounded))
            return false;

        Kind bk = b->type()->kind();
        bool asDouble = bk == Kind::Double ||
                        (bk == Kind::LongDouble && !b->type()->isX87(target));
        if (bk == Kind::Float) {
            float fl = static_cast<float>(l), fr = static_cast<float>(r);
            switch (b->op()) {
            case BinOp::Add: *out = fl + fr; return true;
            case BinOp::Sub: *out = fl - fr; return true;
            case BinOp::Mul: *out = fl * fr; return true;
            case BinOp::Div: if (fr == 0) return false;
                             *out = fl / fr; return true;
            default: return false;
            }
        }
        if (asDouble) {
            double dl = static_cast<double>(l), dr = static_cast<double>(r);
            switch (b->op()) {
            case BinOp::Add: *out = dl + dr; return true;
            case BinOp::Sub: *out = dl - dr; return true;
            case BinOp::Mul: *out = dl * dr; return true;
            case BinOp::Div: if (dr == 0) return false;
                             *out = dl / dr; return true;
            default: return false;
            }
        }
        // The x87 lane. The operands are exact doubles by construction, so the
        // one question is whether this result still fits one - unless an integer
        // past 53 bits fed in, where the only honest answer is the flag.
        if (*past53) *x87Rounded = true;
        const double dl = static_cast<double>(l);
        const double dr = static_cast<double>(r);
        double sum;
        bool fits;
        switch (b->op()) {
        case BinOp::Add: {
            sum = dl + dr;
            const double bb = sum - dl;
            fits = (dl - (sum - bb)) + (dr - bb) == 0.0;
            break;
        }
        case BinOp::Sub: {
            sum = dl - dr;
            const double bb = sum - dl;
            fits = (dl - (sum - bb)) + (-dr - bb) == 0.0;
            break;
        }
        case BinOp::Mul:
            sum = dl * dr;
            fits = std::fma(dl, dr, -sum) == 0.0;
            break;
        case BinOp::Div:
            if (dr == 0) return false;
            sum = dl / dr;
            fits = std::fma(sum, dr, -dl) == 0.0;
            break;
        default: return false;
        }
        if (sum != 0.0 && sum > -2.2250738585072014e-308 &&
                          sum <  2.2250738585072014e-308)
            fits = false;
        if (!fits) *x87Rounded = true;
        *out = sum;
        return true;
    }
    return false;
}

// Whether every value of `from` is a value of `to` - the question
// [dcl.init.list]/7 asks about an integer source that is not a constant.
static bool holdsEvery(const Type *from, const Type *to, const Target &target) {
    if (from->isBool()) return true;
    if (to->isBool()) return false;
    const bool fromUnsigned = !from->isSigned(target);
    const bool toUnsigned = !to->isSigned(target);
    const int fs = from->size(target), ts = to->size(target);
    if (fromUnsigned == toUnsigned) return ts >= fs;
    if (toUnsigned) return false;
    return ts > fs;
}

// Whether an integer survives a trip through the floating type F - the question
// the same paragraph asks of a constant going to `float` or `double`. The bounds
// guard the way back: at or beyond 2^63 there is no integer to come back to.
template <typename F>
static bool roundTrips(long long v, bool isUnsigned) {
    if (isUnsigned) {
        const unsigned long long u = static_cast<unsigned long long>(v);
        const F f = static_cast<F>(u);
        if (f >= static_cast<F>(18446744073709551616.0L)) return false;
        return static_cast<unsigned long long>(f) == u;
    }
    const F f = static_cast<F>(v);
    if (f >= static_cast<F>(9223372036854775808.0L) ||
        f < -static_cast<F>(9223372036854775808.0L)) return false;
    return static_cast<long long>(f) == v;
}

static int floatingRank(const Type *t) {
    if (t->kind() == Kind::Float) return 0;
    if (t->kind() == Kind::Double) return 1;
    return 2;
}

// **[dcl.init.list]/7: a braced initialiser does not narrow.** `char c = {300}`
// gave 44 here without a word. The four conversions, and each "unless a constant
// whose value survives", were measured against clang over forty shapes.
void Parser::checkNarrowing(const Type *to, const Expr &value, std::size_t pos,
                            const std::string &what) {
    const Type *from = value.type();
    if (to == nullptr || from == nullptr) return;
    if (!to->isArithmetic() || !from->isArithmetic()) return;

    const std::string target = "'" + to->describe() + "'" +
                               (what.empty() ? std::string() : " for " + what);
    const std::string fromName = "'" + from->describe() + "'";
    const std::string cannotHoldEvery =
        "a value of type " + fromName + " cannot be narrowed to " + target +
        " in a braced initialiser - '" + to->describe() + "' cannot hold "
        "every " + fromName + ", and braces refuse the conversion unless the "
        "value is a constant that fits. Convert it with a cast, or take the "
        "braces off";

    if (from->isFloating() && to->isInteger())
        src_.fail(pos, "a value of type " + fromName + " cannot be narrowed "
                       "to " + target + " in a braced initialiser - a floating "
                       "value never converts to an integer inside braces, even "
                       "a constant. Convert it with a cast, or take the braces "
                       "off");

    if (from->isInteger()) {
        const bool fromUnsigned = !from->isBool() && !from->isSigned(target_);
        long long v = 0;
        if (!fold(value, &v, pos)) {
            if (to->isFloating())
                src_.fail(pos, "a value of type " + fromName + " cannot be "
                               "narrowed to " + target + " in a braced "
                               "initialiser - an integer converts to a "
                               "floating type inside braces only as a constant "
                               "that survives the trip. Convert it with a cast, "
                               "or take the braces off");
            if (!holdsEvery(from, to, target_)) src_.fail(pos, cannotHoldEvery);
            return;
        }
        const std::string shown = fromUnsigned
            ? std::to_string(static_cast<unsigned long long>(v))
            : std::to_string(v);
        bool fits;
        if (to->isFloating()) {
            if (to->kind() == Kind::LongDouble && to->isX87(target_)) fits = true;
            else if (to->kind() == Kind::Float) fits = roundTrips<float>(v, fromUnsigned);
            else fits = roundTrips<double>(v, fromUnsigned);
            if (!fits)
                src_.fail(pos, shown + " cannot be narrowed to " + target +
                               " - converting it to '" + to->describe() + "' "
                               "and back does not give " + shown + " again, "
                               "and a braced initialiser refuses a conversion "
                               "that changes the value");
            return;
        }
        if (to->isBool()) fits = (v == 0 || v == 1);
        else if (to->size(target_) >= 8)
            // Between two 64-bit types only the sign bit can be lost.
            fits = v >= 0 || fromUnsigned == !to->isSigned(target_);
        else fits = !(fromUnsigned && v < 0) && narrowTo(v, to) == v;
        if (!fits)
            src_.fail(pos, shown + " cannot be narrowed to " + target + " - it "
                           "is not a value '" + to->describe() + "' can hold, "
                           "and a braced initialiser refuses a conversion that "
                           "changes the value. Write one that fits, or convert "
                           "it with a cast");
        return;
    }

    // Floating to floating: only a step down in rank can narrow, and a constant
    // still within range after the step has not. `long double` to `double` is
    // the step even where the target gives the two one representation.
    if (floatingRank(to) >= floatingRank(from)) return;
    long double d = 0;
    // **The two flags are read and dropped, and that is the merge's answer.**
    // foldDouble's flags ask what the build host can represent; this rule asks
    // what the target type can hold. Different questions - see the handover.
    bool past53 = false, x87Rounded = false;
    if (!foldDouble(value, target_, &d, &past53, &x87Rounded))
        src_.fail(pos, cannotHoldEvery);
    const long double limit = to->kind() == Kind::Float
                            ? static_cast<long double>(FLT_MAX)
                            : static_cast<long double>(DBL_MAX);
    if (std::isfinite(d) && std::fabs(d) > limit)
        src_.fail(pos, "this constant cannot be narrowed to " + target +
                       " - it is outside the range '" + to->describe() +
                       "' can hold, and a braced initialiser refuses a "
                       "conversion that changes the value");
}

void Parser::flattenFill(const Type *type, InitCursor &c, int base,
                         std::vector<GlobalPiece> &out) {
    if (c.done()) return;
    Init &item = c.cur();

    if (item.isList) {
        c.at++;
        flattenInit(type, item, base, out);
        return;
    }
    if (const StrLit *s = stringInitialiser(item, type)) {
        c.at++;
        const std::string &text = s->text();
        if (static_cast<long long>(text.size()) > type->length())
            src_.fail(item.pos, "the string has " + std::to_string(text.size()) +
                                " characters and the array holds " +
                                std::to_string(type->length()));

        int w = type->pointee()->size(target_);
        for (std::size_t i = 0; i < text.size(); i++)
            out.push_back(GlobalPiece{ base + static_cast<int>(i) * w, w,
                                       static_cast<long long>(
                                           static_cast<unsigned char>(text[i])), std::string() });
        return;
    }
    if (type->isArray() || type->isStructOrUnion()) {
        flattenAggregate(type, c, base, out);
        return;
    }

    checkNarrowing(type, *item.value, item.pos, std::string());
    flattenScalar(type, item, base, out);
    c.at++;
}

void Parser::flattenAggregate(const Type *type, InitCursor &c, int base,
                              std::vector<GlobalPiece> &out) {
    if (type->isArray()) {
        const Type *elem = type->pointee();
        int step = elem->size(target_);
        for (long long i = 0; i < type->length() && !c.done(); i++)
            flattenFill(elem, c, base + static_cast<int>(i) * step, out);
        return;
    }
    const std::vector<Member> &members = type->members();
    std::size_t count = type->kind() == Kind::Union
                      ? (members.empty() ? std::size_t(0) : std::size_t(1))
                      : members.size();
    for (std::size_t i = 0; i < count && !c.done(); i++) {
        const Member &m = members[i];
        if (m.name.empty()) continue;
        if (m.isBitField())
            src_.fail(c.cur().pos,
                      "a bit-field cannot be initialised at file scope yet - "
                      "assign to it in a function");
        flattenFill(m.type, c, base + m.offset, out);
    }
}

void Parser::flattenInit(const Type *type, Init &in, int base,
                         std::vector<GlobalPiece> &out) {
    // The same value-initialisation `emitInit` spells with `initZero`, said the
    // way a global says it: no pieces at all, which `segmentFor` reads as .bss.
    if (in.isList && in.items.empty()) return;
    if (const StrLit *s = stringInitialiser(in, type)) {
        const std::string &text = s->text();
        if (static_cast<long long>(text.size()) > type->length())
            src_.fail(in.pos, "the string has " + std::to_string(text.size()) +
                              " characters and the array holds " +
                              std::to_string(type->length()));
        int w = type->pointee()->size(target_);
        for (std::size_t i = 0; i < text.size(); i++)
            out.push_back(GlobalPiece{ base + static_cast<int>(i) * w, w,
                                       static_cast<long long>(
                                           static_cast<unsigned char>(text[i])), std::string() });
        return;
    }

    if (type->isArray() || type->isStructOrUnion()) {
        if (!in.isList)
            src_.fail(in.pos, type->isArray()
                              ? "an array at file scope needs a braced initialiser"
                              : "a struct or union at file scope needs a braced "
                                "initialiser");
        // **The same C++11 rule the local path names, and a program could reach
        // past it here.** [dcl.init.aggr]/1 makes a class with a member
        // initialiser no aggregate, so these braces are not aggregate init.
        if (type->isStructOrUnion() && !type->tag().empty() &&
            hasMemberInitialiser(type->tag()))
            src_.fail(in.pos, "'" + type->describe() + "' writes an "
                              "initialiser on a member, so in C++11 it is not "
                              "an aggregate and a braced list cannot "
                              "initialise it - C++14 changed that rule and "
                              "this compiler is C++11");
        InitCursor c{ &in.items, 0 };
        flattenAggregate(type, c, base, out);
        if (!c.done())
            src_.fail(c.cur().pos, "this is full, and there are " +
                                   std::to_string(in.items.size() - c.at) +
                                   " more initialiser(s) after it");
        return;
    }

    if (in.isList) {
        if (in.items.size() != 1)
            src_.fail(in.pos, "this is not an aggregate and takes one value");
        if (!in.items[0].isList)
            checkNarrowing(type, *in.items[0].value, in.items[0].pos,
                           std::string());
        flattenInit(type, in.items[0], base, out);
        return;
    }
    flattenScalar(type, in, base, out);
}

void Parser::flattenScalar(const Type *type, Init &in, int base,
                           std::vector<GlobalPiece> &out) {
    ExprPtr value = decay(std::move(in.value));

    if (type->isFloating()) {
        long double d;
        bool past53 = false, x87Rounded = false;
        if (!foldDouble(*value, target_, &d, &past53, &x87Rounded))
            src_.fail(in.pos, "expected a constant initialiser, and this is not "
                              "a constant");
        // **The same refusal the literal gets, for the same reason.** The gate
        // sat on the literal alone and one folded `+` walked past it, so the
        // emitted constant differed by which machine had built the compiler.
        if (x87Rounded || (past53 && type->isX87(target_)))
            src_.fail(in.pos, "this 'long double' constant expression needs "
                              "more precision than a double holds, and a "
                              "double is what this compiler folds constants "
                              "in - the target would keep more of it than "
                              "the build machine can promise. It is refused "
                              "rather than approximated; compute it at run "
                              "time, or write the value as a 'double'");
        long long bits = 0;
        if (type->isX87(target_)) {

            unsigned long long sig = 0;
            unsigned int hi = 0;
            x87Parts(d, &sig, &hi);
            out.push_back(GlobalPiece{ base, 8, static_cast<long long>(sig),
                                       std::string() });
            out.push_back(GlobalPiece{ base + 8, 2, static_cast<long long>(hi),
                                       std::string() });
            return;
        }
        if (type->kind() == Kind::Float) {
            float f = static_cast<float>(d);
            unsigned int u;
            std::memcpy(&u, &f, sizeof u);
            bits = static_cast<long long>(u);
        } else {
            double dd = static_cast<double>(d);
            unsigned long long u;
            std::memcpy(&u, &dd, sizeof u);
            bits = static_cast<long long>(u);
        }
        out.push_back(GlobalPiece{ base, type->size(target_), bits, std::string() });
        return;
    }

    if (type->isPointer()) {
        std::string sym;
        long long off = 0;
        if (foldAddress(*value, &sym, &off)) {
            // **A derived object's address as a base pointer moves to the base
            // subobject** - `V3 *p = &v4;` with V3 the second base is v4 + 8 -
            // and a virtual base is not at a constant offset at all.
            const Expr *inner = value.get();
            while (const Cast *c = dynamic_cast<const Cast *>(inner)) inner = &c->value();
            if (inner->type()->isPointer() && inner->type()->pointee()->isStructOrUnion() &&
                type->pointee()->isStructOrUnion()) {
                const Type *from = inner->type()->pointee()->unqualified();
                const Type *to = type->pointee()->unqualified();
                const int adjust = publicBaseOffset(from, to);
                if (adjust > 0) off += adjust;
                if (adjust < 0 && from != to && from->hasVirtualBase())
                    src_.fail(in.pos, "expected a constant initialiser: the address of a "
                                      "virtual base is read from the object at run time");
            }
            out.push_back(GlobalPiece{ base, type->size(target_), off, sym });
            return;
        }
    }

    long long v;
    if (!fold(*value, &v, in.pos))
        src_.fail(in.pos, "expected a constant initialiser, and this is not an "
                          "integer constant expression");
    if (type->isInteger()) v = narrowTo(v, type);
    out.push_back(GlobalPiece{ base, type->size(target_), v, std::string() });
}

void Parser::typedefFunctionSuffix(Declared &td) {
    if (!peek().is("(")) return;
    std::vector<const Type *> params;
    bool variadic = false;
    parameterTypes(params, variadic);
    td.type = types_.functionType(td.type, std::move(params), variadic);
}

bool Parser::foldAddress(const Expr &e, std::string *sym, long long *off) const {
    if (const Cast *c = dynamic_cast<const Cast *>(&e))
        return e.type()->isPointer() && foldAddress(c->value(), sym, off);

    if (const StrLit *s = dynamic_cast<const StrLit *>(&e)) {
        *sym = s->label();
        *off = 0;
        return true;
    }

    if (e.type()->isArray() || e.type()->isFunction())
        return addressOfObject(e, sym, off);

    if (const Unary *u = dynamic_cast<const Unary *>(&e)) {
        if (u->op() == '&') return addressOfObject(u->operand(), sym, off);

        if (u->op() == '*') return foldAddress(u->operand(), sym, off);
        return false;
    }

    if (const Binary *b = dynamic_cast<const Binary *>(&e)) {
        if (b->op() != BinOp::Add && b->op() != BinOp::Sub) return false;
        long long n = 0;

        if (foldAddress(b->lhs(), sym, off) && fold(b->rhs(), &n, 0)) {
            *off += (b->op() == BinOp::Add) ? n : -n;
            return true;
        }

        if (b->op() == BinOp::Add && fold(b->lhs(), &n, 0) &&
            foldAddress(b->rhs(), sym, off)) {
            *off += n;
            return true;
        }
        return false;
    }
    return false;
}

bool Parser::addressOfObject(const Expr &e, std::string *sym, long long *off) const {
    if (const Var *v = dynamic_cast<const Var *>(&e)) {

        if (v->isLocal()) return false;
        *sym = v->symbol();
        *off = 0;
        return true;
    }
    if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(&e)) {
        if (m->isBitField()) return false;
        if (!addressOfObject(m->object(), sym, off)) return false;
        *off += m->offset();
        return true;
    }
    if (const Unary *u = dynamic_cast<const Unary *>(&e))
        if (u->op() == '*') return foldAddress(u->operand(), sym, off);
    return false;
}


// **What a class with constructors is initialised from**, read once for every
// storage duration: a local, a static local, a file-scope object and a static
// data member all take the same forms and are refused the same forms.
Parser::CtorInit Parser::readConstructorInitialiser(const Declared &d) {
    CtorInit ci;
    bool valueInitCopied = false;
    // **`C c{};` and `C c = {};` value-initialise.** [dcl.init]/11 sends
    // an empty list to /8, which is the default constructor - and the
    // zeroing before it that `constructObject` puts in for an implicit one.
    if ((peek().is("{") && peekAt(1).is("}")) ||
        (peek().is("=") && peekAt(1).is("{") && peekAt(2).is("}"))) {
        // `= {}` is copy-initialisation and `{}` is not, which is the
        // whole difference an `explicit` default constructor makes.
        valueInitCopied = consume("=");
        expect("{");
        expect("}");
        ci.valueInit = true;
    }

    std::vector<ExprPtr> &args = ci.args;
    ci.copyInit = valueInitCopied;

    // **A braced initialiser, and the answers it has.** An initializer_list
    // constructor takes the braces as a list - [over.match.list]; a member
    // initialiser makes the class no aggregate in C++11; the rest is refused.
    if (peek().is("{") || (peek().is("=") && peekAt(1).is("{"))) {
        const Type *ilType = nullptr;
        const Signature *ilCtor =
            initializerListConstructor(d.type->unqualified(), &ilType);
        if (ilCtor != nullptr) {
            consume("=");
            Init in = parseInitialiser();
            args.push_back(buildInitializerList(ilType, in, d.pos, ci.ilSetup));
            ci.copyInit = true;
            ci.listInit = true;
        } else if (hasMemberInitialiser(d.type->tag())) {
            src_.fail(d.pos, "'" + d.type->describe() + "' writes an "
                             "initialiser on a member, so in C++11 it "
                             "is not an aggregate and a braced list "
                             "cannot initialise it - C++14 changed "
                             "that rule and this compiler is C++11. "
                             "Give the class a constructor, or take "
                             "the member initialiser off");
        } else {
            src_.fail(d.pos, "list-initialisation - '" + d.name +
                             "{...}' calling a constructor - is not "
                             "supported yet unless the class has an "
                             "initializer_list constructor; write the "
                             "arguments in parentheses");
        }
    }
    // **A braced list written as the argument**, `Row r({1, 2})`: [dcl.init]/16
    // and [over.match.list] reach the initializer_list constructor. Built here,
    // where the target type is known - parseArguments cannot build one alone.
    if (!ci.listInit && peek().is("(") && peekAt(1).is("{")) {
        const Type *ilType = nullptr;
        if (initializerListConstructor(d.type->unqualified(), &ilType)
                != nullptr) {
            at_++;                       // the '('
            Init in = parseInitialiser();
            args.push_back(buildInitializerList(ilType, in, d.pos,
                                                ci.ilSetup));
            expect(")");
            ci.listInit = true;
        }
    }
    if (!ci.listInit && consume("(")) {
        if (peek().is(")"))
            src_.fail(d.pos, "'" + d.name + "()' declares a function "
                             "taking nothing and returning '" +
                             d.type->describe() + "' - C++ reads it that "
                             "way and not as a construction. Write '" +
                             d.type->describe() + " " + d.name +
                             ";' for the default constructor");
        parseArguments(args);
    } else if (consume("=")) {
        // **Copy-initialisation.** `X b = a;` is a constructor called with one
        // argument, chosen by the ordinary overload rules. What separates it
        // from `X b(a);` is that an `explicit` constructor may not be picked.
        ci.copyInit = true;
        args.push_back(assign());
    }

    // **One argument of another class that converts to this one through
    // a conversion function** becomes a T here - [over.ics.user], and
    // for `T t(v)` an explicit one too, [over.match.copy]/1.
    if (!ci.listInit && !ci.valueInit)
        convertThroughConversionFunction(args, d.type, !ci.copyInit, d.pos);

    // **An elided copy still needs a copy constructor that may be chosen.**
    // [class.copy]/31 selects and checks it even where the copy itself is
    // elided. Checked here: both branches below reach past `constructLocal`.
    if (ci.copyInit && args.size() == 1 && args[0]->type() != nullptr &&
        args[0]->type()->unqualified() == d.type->unqualified()) {
        // The constructor the rule checks is the one resolution would pick:
        // the move for a source that is not an lvalue and has one, the copy
        // otherwise. An explicit copy beside a plain move does not bite.
        const Signature *mc = moveConstructorOf(d.type->unqualified());
        const Signature *sel = !isLvalue(*args[0]) && mc != nullptr
                             ? mc
                             : copyConstructorOf(d.type->unqualified());
        if (sel != nullptr && sel->isExplicit)
            src_.fail(d.pos, "'" + d.type->describe() + "' has an "
                             "'explicit' " +
                             (sel == mc ? "move" : "copy") +
                             " constructor, so it "
                             "will not be chosen for '" + d.name +
                             " = ...' - write '" +
                             d.type->describe() + " " + d.name +
                             "(...)'. The copy may well be elided, "
                             "and the rule is checked all the same");
    }
    const Signature *mover = moveConstructorOf(d.type->unqualified());
    const bool sameClass =
        args.size() == 1 && args[0]->type() != nullptr &&
        args[0]->type()->unqualified() == d.type->unqualified();
    // An lvalue is what the deleted copy would be asked to take; an
    // xvalue moves, and so does a prvalue - `S d = make();` is a
    // temporary and the move constructor is exactly what it is for.
    if (mover != nullptr && sameClass && isLvalue(*args[0]) &&
        copyConstructorOf(d.type->unqualified()) == nullptr)
        src_.fail(d.pos, "'" + d.type->describe() + "' declares a move "
                         "constructor, so its copy constructor is "
                         "deleted and '" + d.name + "' cannot be built "
                         "from an lvalue - write 'static_cast<" +
                         d.type->describe() + " &&>(...)' to move out "
                         "of it, or give the class a copy constructor");

    ci.trivialCopy =
        sameClass && mover == nullptr &&
        copyConstructorOf(d.type->unqualified()) == nullptr;
        return ci;
}

// ---- Dynamic initialisation: the objects that are built before main --------

// `_GLOBAL__sub_I_<file>`, the name clang gives it: the main file's basename
// with every character that is not a letter, a digit, '_' or '.' made '_'.
std::string Parser::initFunctionSymbol() const {
    std::string file = src_.files().empty() ? std::string("a.cpp")
                                            : src_.files().front();
    const std::size_t slash = file.find_last_of("/\\");
    if (slash != std::string::npos) file = file.substr(slash + 1);
    for (std::size_t i = 0; i < file.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(file[i]);
        if (!std::isalnum(c) && c != '_' && c != '.') file[i] = '_';
    }
    return "_GLOBAL__sub_I_" + file;
}

// The init function's frame is entered the way every other re-entry door
// enters one: the whole state saved, cleared, and the frame put in its place.
Parser::FunctionState Parser::enterInitFunction() {
    FunctionState outer = captureFunctionState();
    clearFunctionState();
    frameSize_ = dynInitFrame_;
    functionName_ = initFunctionSymbol();
    currentFunction_ = functionName_;
    atFunctionBody_ = true;
    initGuardMark_ = guardSlots_.size();
    return outer;
}

// The frame comes back out, and so do the guards made inside - they are not
// part of the state struct, so they are moved by hand. The token index is put
// back to where the initialiser ended, since the restore rewinds it.
void Parser::leaveInitFunction(const FunctionState &outer) {
    dynInitFrame_ = frameSize_;
    for (std::size_t i = initGuardMark_; i < guardSlots_.size(); i++)
        dynInitGuards_.push_back(guardSlots_[i]);
    guardSlots_.resize(initGuardMark_);
    const std::size_t resume = at_;
    restoreFunctionState(outer);
    at_ = resume;
}

// One function for the file, static, its guards cleared at entry as every
// function's are. The backends register it; nothing here calls it.
void Parser::finishDynamicInit(Program &program) {
    if (dynInit_.empty()) return;
    std::vector<StmtPtr> body;
    for (std::size_t i = 0; i < dynInitGuards_.size(); i++)
        body.push_back(StmtPtr(new ExprStmt(setGuard(dynInitGuards_[i], 0))));
    for (std::size_t i = 0; i < dynInit_.size(); i++)
        body.push_back(std::move(dynInit_[i]));
    dynInit_.clear();
    dynInitGuards_.clear();
    const std::string symbol = initFunctionSymbol();
    program.functions.push_back(Function(symbol, types_.get(Kind::Void),
                                         std::vector<Param>(),
                                         StmtPtr(new Block(std::move(body))),
                                         alignTo(dynInitFrame_, 16), true, 0,
                                         false, 0, 0, std::vector<::Local>()));
    program.functions.back().setSymbol(symbol);
    program.initFunction = symbol;
}

// `&f` for a function named by its linkage symbol, the shape a designator
// decays to: a Var of the function's type under an address-of.
ExprPtr Parser::functionAddress(const std::string &symbol, const Type *fnType) {
    Var *f = Var::global(symbol);
    f->setSymbol(symbol);
    ExprPtr fn(f);
    fn->setType(fnType);
    ExprPtr addr(new Unary('&', std::move(fn)));
    addr->setType(types_.pointerTo(fnType));
    return addr;
}

// The object as an expression: a global named by its symbol, or a frame slot.
ExprPtr Parser::objectAt(const Declared &d, const std::string &symbol, int offset) {
    Var *v = symbol.empty() ? Var::local(d.name, offset) : Var::global(d.name);
    if (!symbol.empty()) v->setSymbol(symbol);
    ExprPtr e(v);
    e->setType(d.type);
    return e;
}

// `??__F<scoped>@YAXXZ`, cl's name for the function atexit is handed for one
// object - measured: ??__Fg1@@YAXXZ, ??__Fmg@M@N@@YAXXZ, and for a static
// member the object's whole symbol, ??__F?member@H@@2US@@A@@YAXXZ.
std::string Parser::atexitHelperName(const std::string &scoped) const {
    return "??__F" + scoped + "@YAXXZ";
}

// The construction of one object with static storage duration, in whatever
// frame is in force, its temporaries destroyed after it, and then the
// destructor registered - [basic.start.term], in reverse order of completion.
std::vector<StmtPtr> Parser::buildStaticConstruction(const Declared &d,
                                                     const std::string &symbol,
                                                     const std::string &helper) {
    CtorInit ci = readConstructorInitialiser(d);
    std::vector<StmtPtr> out;
    for (std::size_t z = 0; z < ci.ilSetup.size(); z++)
        out.push_back(std::move(ci.ilSetup[z]));
    if (ci.trivialCopy) {
        ExprPtr store(new Assign(objectAt(d, symbol, 0), std::move(ci.args[0])));
        store->setType(d.type);
        out.push_back(StmtPtr(new ExprStmt(std::move(store))));
    } else {
        out.push_back(constructObject(d, symbol, 0, std::move(ci.args),
                                      ci.copyInit, ci.valueInit));
    }
    flushTemporaries(out);
    registerDestruction(d, symbol, helper, out);
    return out;
}

// **An array of a class with static storage duration**: built by the class's
// loop before main, destroyed last first at exit by a helper calling the
// other loop - __cxa_atexit with the array on Itanium, atexit on Microsoft.
std::vector<StmtPtr> Parser::buildStaticArrayConstruction(const Declared &d,
                                                          const std::string &symbol,
                                                          const std::string &helper) {
    const Type *elem = d.type;
    long long count = 1;
    while (elem->isArray()) { count *= elem->length(); elem = elem->pointee(); }
    const Type *plain = elem->unqualified();
    const Type *ptr = types_.pointerTo(plain);
    const Type *sizeT = types_.get(target_.sizeType());
    if (peek().is("(") || peek().is("=") || peek().is("{"))
        src_.fail(d.pos, "an initialiser for an array of '" + plain->describe() +
                         "' is not supported yet - each element gets the "
                         "default constructor");

    // The array's address, which is its first element's.
    auto arrayAddress = [&]() {
        ExprPtr addr(new Unary('&', objectAt(d, symbol, 0)));
        addr->setType(types_.pointerTo(d.type));
        ExprPtr first(new Cast(ptr, std::move(addr)));
        first->setType(ptr);
        return first;
    };
    std::vector<StmtPtr> out;
    if (overloadsOf(constructorKey(plain->tag())) != nullptr) {
        ExprPtr base = arrayAddress();
        ExprPtr n(new Num(count));
        n->setType(sizeT);
        out.push_back(StmtPtr(new ExprStmt(callVectorLoop(
            vectorConstructor(plain, d.pos), plain, std::move(base), std::move(n), d.pos))));
    }
    if (destructorOf(plain) == nullptr) return out;

    // The helper, in a frame of its own: `(void *)` on Itanium, the array's
    // address arriving as the argument __cxa_atexit was given; nothing on
    // Microsoft, where it names the array itself.
    const bool ms = target_.microsoftNames();
    const std::string fn = ms ? helper : "__cxx1_vec_exit_" + symbol;
    const int savedFrame = frameSize_;
    frameSize_ = 0;
    std::vector<Param> params;
    ExprPtr first;
    if (ms) {
        first = arrayAddress();
    } else {
        const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
        const int argSlot = allocateFrameSlot(voidPtr);
        params.push_back(Param{ voidPtr, argSlot });
        ExprPtr arg(Var::local("a0", argSlot));
        arg->setType(voidPtr);
        first.reset(new Cast(ptr, std::move(arg)));
    }
    first->setType(ptr);
    ExprPtr n2(new Num(count));
    n2->setType(sizeT);
    std::vector<StmtPtr> body;
    body.push_back(StmtPtr(new ExprStmt(callVectorLoop(
        vectorDestructor(plain, d.pos), plain, std::move(first), std::move(n2), d.pos))));
    body.push_back(StmtPtr(new Return(nullptr)));
    current_->functions.push_back(Function(fn, types_.get(Kind::Void),
                                           std::move(params),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), true, 0,
                                           false, 0, d.pos, std::vector<::Local>()));
    current_->functions.back().setSymbol(fn);
    frameSize_ = savedFrame;

    std::vector<ExprPtr> args;
    if (ms) {
        const Type *helperType = types_.functionType(types_.get(Kind::Void),
                                                     std::vector<const Type *>(), false);
        args.push_back(functionAddress(fn, helperType));
        out.push_back(StmtPtr(new ExprStmt(runtimeCall("atexit", types_.intType(), std::move(args)))));
        return out;
    }
    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    std::vector<const Type *> ps;
    ps.push_back(voidPtr);
    args.push_back(functionAddress(fn, types_.functionType(types_.get(Kind::Void), ps, false)));
    ExprPtr asVoid(new Cast(voidPtr, arrayAddress()));
    asVoid->setType(voidPtr);
    args.push_back(std::move(asVoid));
    Var *dso = Var::global("__dso_handle");
    dso->setSymbol("__dso_handle");
    ExprPtr handle(dso);
    handle->setType(types_.get(Kind::Char));
    ExprPtr handleAddr(new Unary('&', std::move(handle)));
    handleAddr->setType(voidPtr);
    args.push_back(std::move(handleAddr));
    current_->usesDsoHandle = true;
    out.push_back(StmtPtr(new ExprStmt(runtimeCall("__cxa_atexit", types_.intType(), std::move(args)))));
    return out;
}

// Itanium: __cxa_atexit(&D1, &object, &__dso_handle), the complete-object
// destructor. Microsoft: atexit(&helper), the helper a function of its own
// that calls the destructor on the object - measured from cl and clang.
void Parser::registerDestruction(const Declared &d, const std::string &symbol,
                                 const std::string &helper,
                                 std::vector<StmtPtr> &into) {
    const Type *cls = d.type->unqualified();
    const Signature *dtor = destructorOf(cls);
    if (dtor == nullptr) return;
    markUsed(dtor);
    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    const Type *clsPtr = types_.pointerTo(cls);

    if (!target_.microsoftNames()) {
        std::vector<const Type *> ps;
        ps.push_back(clsPtr);
        const Type *dtorType = types_.functionType(types_.get(Kind::Void), ps, false);
        std::vector<ExprPtr> args;
        args.push_back(functionAddress(dtor->symbol, dtorType));

        ExprPtr addr(new Unary('&', objectAt(d, symbol, 0)));
        addr->setType(clsPtr);
        ExprPtr asVoid(new Cast(voidPtr, std::move(addr)));
        args.push_back(std::move(asVoid));

        Var *dso = Var::global("__dso_handle");
        dso->setSymbol("__dso_handle");
        ExprPtr handle(dso);
        handle->setType(types_.get(Kind::Char));
        ExprPtr handleAddr(new Unary('&', std::move(handle)));
        handleAddr->setType(voidPtr);
        args.push_back(std::move(handleAddr));
        current_->usesDsoHandle = true;

        into.push_back(StmtPtr(new ExprStmt(
            runtimeCall("__cxa_atexit", types_.intType(), std::move(args)))));
        return;
    }

    // The helper: no parameters, one destructor call, file-local. Built in a
    // frame of its own, as the implicit special members are.
    const int savedFrame = frameSize_;
    frameSize_ = 0;
    std::vector<StmtPtr> body;
    ExprPtr addr(new Unary('&', objectAt(d, symbol, 0)));
    addr->setType(clsPtr);
    body.push_back(StmtPtr(new ExprStmt(destructorCall(std::move(addr), *dtor,
                                                       d.pos))));
    current_->functions.push_back(Function(helper, types_.get(Kind::Void),
                                           std::vector<Param>(),
                                           StmtPtr(new Block(std::move(body))),
                                           alignTo(frameSize_, 16), true, 0,
                                           false, 0, d.pos,
                                           std::vector<::Local>()));
    current_->functions.back().setSymbol(helper);
    frameSize_ = savedFrame;

    const Type *helperType = types_.functionType(types_.get(Kind::Void),
                                                 std::vector<const Type *>(),
                                                 false);
    std::vector<ExprPtr> args;
    args.push_back(functionAddress(helper, helperType));
    into.push_back(StmtPtr(new ExprStmt(
        runtimeCall("atexit", types_.intType(), std::move(args)))));
}

// A file-scope object with a constructor, or a static data member of one,
// built inside the init function. `once` is a template's static member: a
// weak guard beside it keeps a second unit's copy from building it again.
void Parser::dynamicInitialise(const Declared &d, const std::string &symbol,
                               const std::string &helper, bool once) {
    if (d.type->isConst() && !peek().is("=") && !peek().is("(") &&
        !peek().is("{"))
        requireConstInitialised(d.type, d.name, d.pos);
    const FunctionState outer = enterInitFunction();
    std::vector<StmtPtr> built = d.type->isArray()
        ? buildStaticArrayConstruction(d, symbol, helper)
        : buildStaticConstruction(d, symbol, helper);
    if (once) {
        const bool ms = target_.microsoftNames();
        const std::string guard = ms ? symbol + "$guard"
                                     : "_ZGV" + symbol.substr(symbol.compare(0, 2, "_Z") == 0 ? 2 : 0);
        const Type *guardType = types_.get(ms ? Kind::Int : Kind::LongLong);
        current_->globals.push_back(Global{ guard, guard, guardType,
                                            std::vector<GlobalPiece>(), false,
                                            false, false });
        current_->globals.back().isInline = true;
        // The first byte is what Itanium reads; the whole int on Microsoft.
        const Type *readAs = types_.get(ms ? Kind::Int : Kind::UChar);
        Var *seen = Var::global(guard);
        seen->setSymbol(guard);
        ExprPtr flag(seen);
        flag->setType(readAs);
        ExprPtr zero(new Num(0LL));
        zero->setType(readAs);
        ExprPtr fresh(new Binary(BinOp::Eq, std::move(flag), std::move(zero)));
        fresh->setType(types_.get(Kind::Bool));

        Var *mark = Var::global(guard);
        mark->setSymbol(guard);
        ExprPtr marked(mark);
        marked->setType(readAs);
        ExprPtr one(new Num(1LL));
        one->setType(readAs);
        ExprPtr set(new Assign(std::move(marked), std::move(one)));
        set->setType(readAs);

        std::vector<StmtPtr> body;
        body.push_back(StmtPtr(new ExprStmt(std::move(set))));
        for (std::size_t i = 0; i < built.size(); i++)
            body.push_back(std::move(built[i]));
        built.clear();
        built.push_back(StmtPtr(new If(std::move(fresh),
                                       StmtPtr(new Block(std::move(body))),
                                       StmtPtr())));
    }
    for (std::size_t i = 0; i < built.size(); i++)
        dynInit_.push_back(std::move(built[i]));
    leaveInitFunction(outer);
}

// [stmt.dcl]/4 under the ABI's own guard: __cxa_guard_acquire / _release
// around the construction on Itanium; _Init_thread_header / _footer with the
// guard at -1 on Microsoft, without cl's TLS epoch fast path - CONFORMANCE.md.
StmtPtr Parser::guardOnce(const std::string &symbol, std::vector<StmtPtr> body) {
    const bool ms = target_.microsoftNames();
    const std::string guard = symbol + (ms ? "$guard" : ".guard");
    const Type *guardType = types_.get(ms ? Kind::Int : Kind::LongLong);
    current_->globals.push_back(Global{ guard, guard, guardType,
                                        std::vector<GlobalPiece>(), false,
                                        true, false });
    const Type *guardPtr = types_.pointerTo(guardType);
    auto guardAddr = [&]() {
        Var *g = Var::global(guard);
        g->setSymbol(guard);
        ExprPtr e(g);
        e->setType(guardType);
        ExprPtr a(new Unary('&', std::move(e)));
        a->setType(guardPtr);
        return a;
    };
    auto guardIs = [&](long long value, Kind readAs) {
        Var *g = Var::global(guard);
        g->setSymbol(guard);
        ExprPtr e(g);
        e->setType(types_.get(readAs));
        ExprPtr n(new Num(value));
        n->setType(types_.get(readAs));
        ExprPtr c(new Binary(BinOp::Eq, std::move(e), std::move(n)));
        c->setType(types_.get(Kind::Bool));
        return c;
    };

    if (!ms) {
        std::vector<ExprPtr> a1;
        a1.push_back(guardAddr());
        ExprPtr acquired = runtimeCall("__cxa_guard_acquire", types_.intType(),
                                       std::move(a1));
        std::vector<ExprPtr> a2;
        a2.push_back(guardAddr());
        body.push_back(StmtPtr(new ExprStmt(
            runtimeCall("__cxa_guard_release", types_.get(Kind::Void),
                        std::move(a2)))));
        std::vector<StmtPtr> inner;
        inner.push_back(StmtPtr(new If(std::move(acquired),
                                       StmtPtr(new Block(std::move(body))),
                                       StmtPtr())));
        return StmtPtr(new If(guardIs(0, Kind::UChar),
                              StmtPtr(new Block(std::move(inner))), StmtPtr()));
    }

    std::vector<StmtPtr> whole;
    std::vector<ExprPtr> a1;
    a1.push_back(guardAddr());
    whole.push_back(StmtPtr(new ExprStmt(
        runtimeCall("_Init_thread_header", types_.get(Kind::Void),
                    std::move(a1)))));
    std::vector<ExprPtr> a2;
    a2.push_back(guardAddr());
    body.push_back(StmtPtr(new ExprStmt(
        runtimeCall("_Init_thread_footer", types_.get(Kind::Void),
                    std::move(a2)))));
    whole.push_back(StmtPtr(new If(guardIs(-1, Kind::Int),
                                   StmtPtr(new Block(std::move(body))),
                                   StmtPtr())));
    return StmtPtr(new Block(std::move(whole)));
}

// A static local's symbol: the function's *mangled* symbol and the object's
// name, numbered where a name is declared twice in one function. The bare
// name was used until 2026-09-17, and S::f, T::f and f(double) all made `f.n`.
std::string Parser::uniqueStaticSymbol(const std::string &name) {
    const std::string &owner = currentFunction_.empty() ? functionName_
                                                        : currentFunction_;
    std::string symbol = owner + "." + name;
    for (int n = 1; ; n++) {
        bool taken = false;
        for (const std::string &used : staticSymbols_)
            if (used == symbol) { taken = true; break; }
        if (!taken) break;
        symbol = owner + "." + name + "." + std::to_string(n);
    }
    staticSymbols_.push_back(symbol);
    return symbol;
}

// The Microsoft helper for a static local is scoped to its function -
// `??__Floc@?1??f@@YAHXZ@YAXXZ` - with an undecorated owner written ?name@@9.
void Parser::staticLocalWithConstructor(const Declared &d,
                                        std::vector<StmtPtr> &inits) {
    const std::string symbol = uniqueStaticSymbol(d.name);
    std::string helper;
    if (target_.microsoftNames()) {
        const std::string owner =
            !currentFunction_.empty() && currentFunction_[0] == '?'
                ? currentFunction_ : "?" + currentFunction_ + "@@9";
        helper = atexitHelperName(d.name + "@?1?" + owner);
    }
    std::vector<StmtPtr> body = d.type->isArray()
        ? buildStaticArrayConstruction(d, symbol, helper)
        : buildStaticConstruction(d, symbol, helper);
    declareStaticLocal(d.name, d.type, d.pos, symbol);
    locals_.back().isConst = d.type->isConst();
    // Not `isConst`: the constructor writes it, so it cannot live in .rodata.
    current_->globals.push_back(Global{ symbol, symbol, d.type,
                                        std::vector<GlobalPiece>(), false,
                                        true, false });
    inits.push_back(guardOnce(symbol, std::move(body)));
}

// A reference with static storage duration: `&global` goes into the image,
// anything else is bound where `into` says - the init function, or a static
// local's guarded statement. A temporary is refused by name.
void Parser::bindStaticReference(const Declared &d, const std::string &symbol,
                                 std::vector<GlobalPiece> &pieces, bool &hasInit,
                                 std::vector<StmtPtr> *into) {
    if (!peek().is("="))
        src_.fail(d.pos, "'" + d.name + "' is a reference and has to be "
                         "initialised here - there is no later assignment "
                         "that would bind it, only one that writes through it");
    at_++;
    ExprPtr init = assign();
    if (!isGlvalue(*init))
        src_.fail(d.pos, "'" + d.name + "' is a reference with static storage "
                         "duration, and binding it to a temporary is not "
                         "supported yet - the temporary would need static "
                         "storage of its own. Name the object and bind to that");
    ExprPtr addr = bindReference(d.type, std::move(init), d.pos,
                                 "'" + d.name + "'");
    const Type *slot = types_.pointerTo(d.type->referent());
    if (const Unary *u = dynamic_cast<const Unary *>(addr.get()))
        if (u->op() == '&')
            if (const Var *v = dynamic_cast<const Var *>(&u->operand()))
                if (!v->isLocal()) {
                    GlobalPiece p;
                    p.offset = 0;
                    p.size = slot->size(target_);
                    p.value = 0;
                    p.symbol = v->symbol();
                    pieces.push_back(p);
                    hasInit = true;
                    return;
                }
    Var *target = Var::global(d.name);
    target->setSymbol(symbol);
    ExprPtr t(target);
    t->setType(slot);
    ExprPtr bind(new Assign(std::move(t), std::move(addr)));
    bind->setType(slot);
    into->push_back(StmtPtr(new ExprStmt(std::move(bind))));
    flushTemporaries(*into);
}
