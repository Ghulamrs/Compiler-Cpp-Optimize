// The parser: initialisers. Braced initialisers flattened onto an object's
// bytes, the run-time stores a local needs against the static image a global
// gets, and the integer, floating and address folding that chooses between them.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <climits>
#include <cmath>
#include <cstring>

// Whether `base` is a base class of `cls`, at any depth.
static bool hasBaseClass(const Type *cls, const Type *base) {
    for (const Type::BaseSpec &b : cls->unqualified()->bases())
        if (b.type->unqualified() == base || hasBaseClass(b.type, base)) return true;
    return false;
}

Parser::Init Parser::parseInitialiser() {
    Init in;
    in.pos = peek().pos;
    if (consume("{")) {
        const Nesting deep(this, in.pos, "braced list");
        in.isList = true;
        // **`{}` is not an empty list, it is value-initialisation.** [dcl.init]/11
        // sends it to /8, the paragraph `T()` already follows here; every reader
        // of an `Init` below takes no items as "zero it", so none tests for this.
        if (consume("}")) return in;
        for (;;) {
            if (expandPackInitialiser(in.items)) {
            } else if (atBareFunctionArgument(true)) {
                // An overloaded name waits for the element it initialises, as a call argument does
                // for its parameter - `{ &S::f, &S::f }` for two pointers to members (a4).
                OverloadTarget waits(*this, nullptr, true);
                Init item;
                item.pos = peek().pos;
                item.value = assign();
                in.items.push_back(std::move(item));
            } else {
                in.items.push_back(parseInitialiser());
            }
            if (consume("}")) break;
            expect(",");
            if (consume("}")) break;
        }
        return in;
    }
    in.value = assign();
    return in;
}

// `T x{...}` - direct-list-initialisation, [dcl.init.list]/3: for an aggregate or a scalar
// the same braces `= {...}` reads, and the readers of the Init that follows do the rest.
bool Parser::atBracedInitialiser(const std::string &) {
    return peek().is("{");
}

// From an `=`, `(` or `{` to the end of its initialiser: a depth-0 `,` or `;`, the
// bracket that closes a `(` or `{` form, or the `)` that ends a condition.
bool Parser::initialiserNames(const std::string &name) const {
    const Token &first = peek();
    const bool bracketed = first.is("(") || first.is("{");
    if (!bracketed && !first.is("=")) return false;
    int depth = 0;
    for (std::size_t k = 0; ; k++) {
        const Token &t = peekAt(k);
        if (t.kind == TokenKind::End) return false;
        if (t.is("(") || t.is("[") || t.is("{")) { depth++; continue; }
        if (t.is(")") || t.is("]") || t.is("}")) {
            if (--depth < 0 || (bracketed && depth == 0)) return false;
            continue;
        }
        if (depth == 0 && (t.is(",") || t.is(";"))) return false;
        if (t.kind != TokenKind::Ident || t.text != name) continue;
        const Token &before = peekAt(k - 1);
        if (!before.is(".") && !before.is("->") && !before.is("::")) return true;
    }
}

// A string literal's code units as its element type has them: bytes for char, UTF-16 units for char16_t, code
// points for char32_t and wchar_t - [lex.string]/8-10, the text being UTF-8.
static std::vector<long long> literalUnits(const StrLit *s) {
    std::vector<long long> units;
    const std::string &text = s->text();
    const Kind k = s->type()->pointee()->kind();
    if (k == Kind::Char || k == Kind::SChar || k == Kind::UChar) {
        for (unsigned char ch : text) units.push_back(ch);
        return units;
    }
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        long long cp = extra == 0 ? c : c & (0x3F >> extra);
        i++;
        for (int n = 0; n < extra && i < text.size(); n++, i++) cp = (cp << 6) | (static_cast<unsigned char>(text[i]) & 0x3F);
        if (k == Kind::Char16 && cp >= 0x10000) {
            units.push_back(0xD800 + ((cp - 0x10000) >> 10));
            units.push_back(0xDC00 + ((cp - 0x10000) & 0x3FF));
        } else {
            units.push_back(cp);
        }
    }
    return units;
}

const StrLit *Parser::stringInitialiser(const Init &given, const Type *type) {
    // **[dcl.init.string]/1: "optionally enclosed in braces"** - `char s[] = {"ab"};` is the string,
    // and was read as a list of chars whose first was a pointer (R14).
    const Init &in = given.isList && given.items.size() == 1 && !given.items[0].isList
                   ? given.items[0] : given;
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

// The literal's own type counts its '\0' and its code units, which its text does not.
void Parser::requireStringFits(const Type *array, const StrLit *s, std::size_t pos) {
    const long long need = s->type()->length();
    if (need <= array->length()) return;
    src_.fail(pos, "the array holds " + std::to_string(array->length()) +
                   " characters and the string needs " + std::to_string(need) +
                   " with its terminating '\\0' - C may drop the '\\0', C++ may not "
                   "([dcl.init.string]/2). Make the array one longer, or leave its "
                   "length out");
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
            // An anonymous union's members are one slot, taking one initialiser as the union would.
            if (members[i].continuesUnion(i > 0 ? &members[i - 1] : nullptr)) continue;
            skipInit(members[i].anonUnion != nullptr ? members[i].anonUnion : members[i].type, c);
        }
        return;
    }
    c.at++;
}

long long Parser::inferredLength(const Init &in, const Type *element, std::size_t pos) {
    if (const StrLit *s = stringInitialiser(in, types_.arrayOf(element, 1)))
        return static_cast<long long>(literalUnits(s).size()) + 1;
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
    ExprPtr e;
    if (initRootMember_ != nullptr) {
        e = thisMember(initRootThis_, initRootClass_, *initRootMember_);
        e->setType(initRootMember_->type);
    } else {
        e = objectRef(name);
    }
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

// [dcl.init.aggr]/1: no constructor the program wrote, no private or protected member, no base, no
// virtual function, and - in C++11 - no member initialiser. The implicit constructors do not count.
bool Parser::isAggregateClass(const Type *t) const {
    t = t->unqualified();
    if (!t->isStructOrUnion() || t->tag().empty() || !t->bases().empty() || t->polymorphic() ||
        hasMemberInitialiser(t->tag()))
        return false;
    for (const Member &m : t->members())
        if (m.declaredIn == nullptr && m.access != Access::Public) return false;
    // [dcl.init.aggr]/1: no user-provided constructor, and one defaulted or deleted where it is first
    // declared is not user-provided ([dcl.fct.def.default]/4) - `atomic_flag f = ATOMIC_FLAG_INIT;`.
    if (const std::vector<std::size_t> *set = overloadsOf(constructorKey(t->tag())))
        for (std::size_t i : *set)
            if (!functions_[i].implicit && !functions_[i].deleted) return false;
    return true;
}

// `= {` or `{` ahead: the list an aggregate is initialised from.
bool Parser::bracedInitialiserAhead() const {
    return peek().is("{") || (peek().is("=") && peekAt(1).is("{"));
}

// A class built by calling one of its constructors, rather than member by member from a list.
bool Parser::constructedClass(const Type *t) const {
    t = t->unqualified();
    return t->isStructOrUnion() && !t->tag().empty() && hasConstructors(t->tag()) && !isAggregateClass(t);
}

// Whether building `t` runs a constructor anywhere inside it.
bool Parser::needsConstruction(const Type *t) const {
    t = t->unqualified();
    while (t->isArray()) t = t->pointee()->unqualified();
    if (!t->isStructOrUnion()) return false;
    if (constructedClass(t)) return true;
    for (const Member &m : t->members())
        if (m.declaredIn == nullptr && needsConstruction(m.type)) return true;
    return false;
}

// **An aggregate's member whose class has a constructor is built where the list reaches it**,
// [dcl.init.aggr]/2: copy-initialised from its value, or value-initialised where the list ends.
void Parser::initConstruct(const std::string &name, std::vector<InitStep> &path, const Type *type,
                           std::vector<ExprPtr> args, bool valueInit, std::size_t pos,
                           std::vector<StmtPtr> &out) {
    const Type *cls = type->unqualified();
    // A prvalue of the class itself is built where the member is, the copy left out ([class.copy]/31).
    if (args.size() == 1) {
        ExprPtr at(new Unary('&', targetFor(name, path)));
        at->setType(types_.pointerTo(cls));
        if (constructInPlace(*args[0], cls, at)) {
            out.push_back(StmtPtr(new ExprStmt(std::move(args[0]))));
            return;
        }
    }
    if (args.size() == 1 && args[0]->type() != nullptr && args[0]->type()->unqualified() == cls &&
        !cls->nonTrivialCopy()) {
        initStore(name, path, std::move(args[0]), pos, out);
        return;
    }
    checkNotAbstract(cls, pos, "'" + name + "'");
    if (!valueInit) copyFromOtherClass(cls, args, pos);
    const Signature &ctor = resolveOverload(constructorKey(cls->tag()), args, pos);
    applyDefaults(ctor, args, pos);
    if (ctor.isExplicit && !valueInit)
        src_.fail(pos, "'" + cls->describe() + "' has a constructor taking these arguments and it is "
                       "'explicit', so copy-initialising a member of '" + name + "' will not choose it");
    if (ctor.access != Access::Public && !insideAccessOf(cls, ctor.access) && !isFriendOf(cls))
        src_.fail(pos, "'" + cls->describe() + "' has no public constructor taking these arguments - "
                       "the one that matches is " + (ctor.access == Access::Private ? "private" : "protected"));
    const Type *thisType = types_.pointerTo(cls);
    ExprPtr addr(new Unary('&', targetFor(name, path)));
    addr->setType(thisType);
    std::vector<ExprPtr> all;
    all.push_back(std::move(addr));
    for (ExprPtr &a : args) all.push_back(std::move(a));
    std::vector<const Type *> full(1, thisType);
    for (const Type *p : ctor.params) full.push_back(p);
    ExprPtr call = completeCall(cls->tag(), ctor.symbol, nullptr, types_.get(Kind::Void), full, false, pos,
                                std::move(all));
    if (valueInit && ctor.implicit)
        if (ExprPtr chain = zeroChain(*targetFor(name, path), cls)) {
            ExprPtr seq(new Comma(std::move(chain), std::move(call)));
            seq->setType(types_.get(Kind::Void));
            call = std::move(seq);
        }
    out.push_back(StmtPtr(new ExprStmt(std::move(call))));
}

void Parser::initStore(const std::string &name, std::vector<InitStep> &path,
                       ExprPtr value, std::size_t pos,
                       std::vector<StmtPtr> &out) {
    ExprPtr target = targetFor(name, path);
    const Type *to = target->type();
    resolveOverloadPlaceholder(value, to, pos);
    // **A reference member is bound, not assigned** - [dcl.init.aggr]/2 with [dcl.init.ref]: its slot
    // holds the address, and a temporary it takes lives as long as it does ([class.temporary]/5).
    if (to->isReference()) {
        ExprPtr addr = bindReference(to, std::move(value), pos, "'" + name + "'");
        extendTemporary(*addr, name + "$member" + std::to_string(refTemps_++));
        const Type *slot = types_.pointerTo(to->referent());
        target->setType(slot);
        ExprPtr bind(new Assign(std::move(target), std::move(addr)));
        bind->setType(slot);
        out.push_back(StmtPtr(new ExprStmt(std::move(bind))));
        return;
    }
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
    if (constructedClass(type)) {
        initConstruct(name, path, type, std::vector<ExprPtr>(), true, pos, out);
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
    // Zero is a value of every enumeration; this store is not the program converting an int.
    if (type->unqualified()->isEnumeration()) z->setType(type->unqualified());
    initStore(name, path, std::move(z), pos, out);
}

void Parser::emitString(const std::string &name, std::vector<InitStep> &path,
                        const Type *type, const StrLit *s, std::size_t pos,
                        std::vector<StmtPtr> &out) {
    long long len = type->length();
    const std::vector<long long> units = literalUnits(s);
    requireStringFits(type, s, pos);
    for (long long i = 0; i < len; i++) {
        path.push_back(InitStep{ nullptr, i });
        long long ch = i < static_cast<long long>(units.size()) ? units[static_cast<std::size_t>(i)] : 0L;
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

    // A member of a class with a constructor takes one value, or a list of its arguments.
    if (constructedClass(type)) {
        c.at++;
        std::vector<ExprPtr> args;
        if (!item.isList) args.push_back(decay(std::move(item.value)));
        for (Init &a : item.items) {
            if (a.isList) src_.fail(a.pos, "a braced list inside the arguments of '" + type->describe() +
                                           "''s constructor is not supported yet - write the value");
            args.push_back(decay(std::move(a.value)));
        }
        initConstruct(name, path, type, std::move(args), item.isList && item.items.empty(), item.pos, out);
        return;
    }
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
    // **[dcl.init.aggr]/11: an expression that converts to the member initialises it** - `W<G> w{g}` copies
    // g into the G member; braces are elided only where it does not, into the member's own first member.
    if (type->isStructOrUnion() && !type->isMemberFunctionPointer() && item.value != nullptr &&
        item.value->type() != nullptr) {
        const Type *vt = item.value->type();
        if (vt->isReference()) vt = vt->referent();
        vt = vt->unqualified();
        const Type *to = type->unqualified();
        if (vt == to || (vt->isStructOrUnion() && publicBaseOffset(vt, to) >= 0)) {
            c.at++;
            initStore(name, path, decay(std::move(item.value)), item.pos, out);
            return;
        }
    }
    // A pointer to a member function is laid out as a struct here, and it is still one value:
    // `{&B::vf, &B::nf}` fills two of them, not the first one's fields (1.27, 2026-10-21, review
    // of 1.25, item 3 - the array's first element was asked to take `&B::vf` as a `void *`).
    if (type->isArray() || (type->isStructOrUnion() && !type->isMemberFunctionPointer())) {
        emitAggregate(name, path, type, c, item.pos, out);
        return;
    }

    c.at++;
    checkNarrowing(type, *item.value, item.pos, "'" + name + "'");
    initStore(name, path, type->isReference() ? std::move(item.value) : decay(std::move(item.value)),
              item.pos, out);
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
        // An anonymous union's members are one slot: the step is the union itself, at its offset, and
        // one initialiser reaches its first member as it would a named union's - [dcl.init.aggr].
        if (members[i].continuesUnion(i > 0 ? &members[i - 1] : nullptr)) continue;
        const Member slot{ std::string(), members[i].anonUnion, members[i].offset };
        const bool grouped = members[i].anonUnion != nullptr;
        path.push_back(InitStep{ grouped ? &slot : &members[i], 0 });
        const Type *t = grouped ? members[i].anonUnion : members[i].type;
        if (c.done()) initZero(name, path, t, pos, out);
        else          emitFill(name, path, t, c, out);
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
        // [dcl.init.list]/3: a list with values initialises an aggregate member by member, and a class
        // that is not one - a base, a private member, a member initialiser in C++11 - only by a constructor.
        if (!in.items.empty() && !type->unqualified()->tag().empty() && !type->isMemberFunctionPointer() &&
            !isAggregateClass(type))
            src_.fail(in.pos, "'" + type->unqualified()->describe() + "' is not an aggregate - it has a base, a "
                              "member that is not public, a virtual function or a member initialiser - so a "
                              "braced list initialises it through a constructor, and it has none taking these "
                              "values ([dcl.init.aggr]/1)");
    } else {
        // A reference member binds what it is given, an array or a function included - `int (&ra)[2]`
        // takes `arr` itself, which decayed first was an `int *` it cannot hold (R22).
        if (!in.isList) {
            initStore(name, path, type->isReference() ? std::move(in.value) : decay(std::move(in.value)),
                      in.pos, out);
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

// A class with a constructor takes the list through an initializer_list constructor or as the
// arguments of one ([over.match.list]); an array or an aggregate is filled element by element, as
// a local of its type would be, with `this->m` where the local's name would stand.
StmtPtr Parser::listInitialiseMember(const std::string &cls, const Type *type, const Member &m,
                                     int thisSlot, Init &in, std::size_t pos) {
    std::vector<StmtPtr> out;
    const Type *t = m.type->unqualified();
    if (t->isStructOrUnion() && constructedClass(t)) {
        // `{}` value-initialises: zeroed first where the default constructor is the implicit one.
        if (in.items.empty()) {
            const Signature *ctor = defaultConstructorOf(t);
            if (ctor != nullptr && ctor->implicit) {
                ExprPtr field = thisMember(thisSlot, type, m);
                field->setType(m.type);
                if (ExprPtr chain = zeroChain(*field, t)) out.push_back(StmtPtr(new ExprStmt(std::move(chain))));
            }
        }
        std::vector<ExprPtr> args;
        const Type *ilType = nullptr;
        if (!in.items.empty() && initializerListConstructor(t, &ilType) != nullptr && initializerListFits(ilType, in))
            args.push_back(buildInitializerList(ilType, in, pos, out));
        else
            listConstructor(t, in, args, false, pos);
        StmtPtr made = constructMember(cls, type, m, thisSlot, args, pos, false);
        if (made != nullptr) out.push_back(std::move(made));
    } else {
        const Member *rootWas = initRootMember_;
        const Type *classWas = initRootClass_;
        const int thisWas = initRootThis_;
        initRootMember_ = &m;
        initRootClass_ = type;
        initRootThis_ = thisSlot;
        std::vector<InitStep> path;
        emitInit(m.name, path, m.type, in, out);
        initRootMember_ = rootWas;
        initRootClass_ = classWas;
        initRootThis_ = thisWas;
    }
    if (out.size() == 1) return std::move(out[0]);
    return StmtPtr(new Block(std::move(out)));
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
        if (const ConstexprSlot *slot = constexprSlot(*v)) {
            if (slot->obj != nullptr) return false;
            *out = slot->floating ? slot->d : static_cast<long double>(slot->i);
            return true;
        }
        // A named integral constant, `CI * 0.9` - widened as an integer operand is ([expr.const]/2).
        auto integral = [&](long long i) {
            const bool sign = e.type() == nullptr || e.type()->isSigned(target);
            unsigned long long a = sign && i < 0 ? 0ULL - static_cast<unsigned long long>(i)
                                                 : static_cast<unsigned long long>(i);
            while (a != 0 && (a & 1) == 0) a >>= 1;
            if (a >= (1ULL << 53)) *past53 = true;
            *out = sign ? static_cast<long double>(i) : static_cast<long double>(static_cast<unsigned long long>(i));
            return true;
        };
        if (v->isLocal()) {
            if (const Local *l = findLocal(v->name())) {
                if (l->isConstantDouble) { *out = l->constantDouble; return true; }
                if (l->isConstantValue) return integral(l->constantValue);
            }
            return false;
        }
        if (const GlobalSym *g = findGlobal(v->name())) {
            if (g->isConstantDouble) { *out = g->constantDouble; return true; }
            if (g->isConstantValue) return integral(g->constantValue);
        }
        // As in fold(): a static member lives under its symbol, not in globals_.
        if (const StaticConst *sc = findStaticConst(v->symbol())) {
            if (sc->dknown) { *out = sc->dvalue; return true; }
            if (sc->known) return integral(sc->value);
        }
        return false;
    }
    // A member of a constant object, as fold() reads one.
    if (dynamic_cast<const MemberAccess *>(&e) != nullptr ||
        (dynamic_cast<const Unary *>(&e) != nullptr && static_cast<const Unary &>(e).op() == '*')) {
        ConstLeaf leaf;
        if (!foldLeaf(e, &leaf, 0)) return false;
        *out = leaf.type->isFloating() ? leaf.d
             : leaf.type->isSigned(target) ? static_cast<long double>(leaf.i)
             : static_cast<long double>(static_cast<unsigned long long>(leaf.i));
        return true;
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
    // **A call to a constexpr function, as fold() runs one** (A11).
    if (const Call *c = dynamic_cast<const Call *>(&e)) {
        // No expression carries a position, so the depth limit is reported at
        // the constexpr function's own definition.
        auto it = constexprFns_.find(c->symbol());
        const ConstexprFn *fn = nullptr;
        if (!enterConstexprCall(*c, &fn, it == constexprFns_.end() ? 0 : it->second.pos))
            return false;
        const bool ok = foldDouble(*fn->value, target, out, past53, x87Rounded);
        constexprFrames_.pop_back();
        if (ok && !e.type()->isFloating())
            *out = static_cast<long double>(static_cast<long long>(*out));
        return ok;
    }
    if (const Conditional *c = dynamic_cast<const Conditional *>(&e)) {
        long long t;
        if (!fold(c->cond(), &t, 0)) return false;
        return foldDouble(t ? c->thenArm() : c->elseArm(), target, out, past53, x87Rounded);
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
    // **A pointer to bool narrows** (P1957, a defect report clang and cl apply to C++11): `bool b = {p};`
    // with `bool b = p;` beside it allowed.
    if (to->unqualified()->isBool() &&
        (from->isPointer() || from->isMemberPointer() || from->isMemberFunctionPointer()))
        src_.fail(pos, "a '" + from->describe() + "' cannot be narrowed to 'bool' in a braced initialiser" +
                       (what.empty() ? std::string() : " for " + what) +
                       " - braces refuse the pointer-to-bool conversion; write '!= nullptr' or drop the braces");
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
        const std::vector<long long> units = literalUnits(s);
        requireStringFits(type, s, item.pos);

        int w = type->pointee()->size(target_);
        for (std::size_t i = 0; i < units.size(); i++)
            out.push_back(GlobalPiece{ base + static_cast<int>(i) * w, w, units[i], std::string() });
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
        if (m.continuesUnion(i > 0 ? &members[i - 1] : nullptr)) continue;
        if (m.anonUnion != nullptr) { flattenFill(m.anonUnion, c, base + m.offset, out); continue; }
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
        const std::vector<long long> units = literalUnits(s);
        requireStringFits(type, s, in.pos);
        int w = type->pointee()->size(target_);
        for (std::size_t i = 0; i < units.size(); i++)
            out.push_back(GlobalPiece{ base + static_cast<int>(i) * w, w, units[i], std::string() });
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
    // `typedef int (*Picker(double))(double);` - a function that returns a pointer to a function. The name's own
    // parameter list stood inside the parentheses, where the declarator passed over it: it is read from there
    // ([dcl.fct]; until 1.75 the list was dropped and the typedef named the pointer the function returns).
    if (td.paramsAt != 0) {
        const std::size_t after = at_;
        at_ = td.paramsAt;
        std::vector<const Type *> inner;
        bool innerVariadic = false;
        parameterTypes(inner, innerVariadic);
        at_ = after;
        td.type = types_.functionType(td.type, std::move(inner), innerVariadic);
        td.paramsAt = 0;
        return;
    }
    if (!peek().is("(")) return;
    std::vector<const Type *> params;
    bool variadic = false;
    parameterTypes(params, variadic);
    td.type = types_.functionType(td.type, std::move(params), variadic);
}

bool Parser::foldAddress(const Expr &e, std::string *sym, long long *off) const {
    // A constexpr call's pointer parameter, holding a named address.
    if (const Var *v = dynamic_cast<const Var *>(&e))
        if (const ConstexprSlot *slot = constexprSlot(*v)) {
            if (!slot->pointer || slot->address.empty()) return false;
            *sym = slot->address;
            *off = slot->i;
            return true;
        }
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

    // **A braced initialiser, [over.match.list].** An initializer_list constructor takes the
    // braces as a list; otherwise the elements are the arguments and every constructor is a
    // candidate - unless the class wrote only a member initialiser, which is C++14's aggregate.
    if (peek().is("{") || (peek().is("=") && peekAt(1).is("{"))) {
        const Type *ilType = nullptr;
        const Signature *ilCtor =
            initializerListConstructor(d.type->unqualified(), &ilType);
        if (isInitializerListType(d.type)) {
            // [dcl.init.list]/3: the list object itself, over its backing array.
            consume("=");
            Init in = parseInitialiser();
            args.push_back(buildInitializerList(d.type->unqualified(), in, d.pos, ci.ilSetup));
            ci.copyInit = true;
            ci.listInit = true;
        } else if (ilCtor != nullptr) {
            const bool copyList = consume("=");
            Init in = parseInitialiser();
            if (initializerListFits(ilType, in)) {
                args.push_back(buildInitializerList(ilType, in, d.pos, ci.ilSetup));
                ci.copyInit = true;
            } else {
                listConstructor(d.type->unqualified(), in, args, copyList, d.pos);
                ci.copyInit = copyList;
            }
            ci.listInit = true;
        } else if (hasMemberInitialiser(d.type->tag()) &&
                   onlyImplicitConstructors(d.type->tag())) {
            src_.fail(d.pos, "'" + d.type->describe() + "' writes an "
                             "initialiser on a member, so in C++11 it "
                             "is not an aggregate and a braced list "
                             "cannot initialise it - C++14 changed "
                             "that rule and this compiler is C++11. "
                             "Give the class a constructor, or take "
                             "the member initialiser off");
        } else {
            const bool copyList = consume("=");
            Init in = parseInitialiser();
            if (listConstructorTemplate(d.type->unqualified(), in, &ilType, d.pos) != nullptr &&
                initializerListFits(ilType, in)) {
                args.push_back(buildInitializerList(ilType, in, d.pos, ci.ilSetup));
                ci.copyInit = true;
            } else {
                listConstructor(d.type->unqualified(), in, args, copyList, d.pos);
                ci.copyInit = copyList;
            }
            ci.listInit = true;
        }
    }
    // **A braced list as the argument**, `Row r({1, 2})`: [dcl.init]/16 and
    // [over.match.list] reach the initializer_list constructor, built here;
    // only as the one argument (`D v({1, 2}, alloc)` goes the general road).
    bool onlyList = false;
    if (!ci.listInit && peek().is("(") && peekAt(1).is("{")) {
        int depth = 0;
        for (std::size_t k = 1;; k++) {
            const Token &t = peekAt(k);
            if (t.kind == TokenKind::End) break;
            if (t.is("{")) depth++;
            else if (t.is("}") && --depth == 0) { onlyList = peekAt(k + 1).is(")"); break; }
        }
    }
    if (onlyList) {
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
        buildBracedArguments(constructorKey(d.type->tag()), args, d.pos);
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
    if (ci.copyInit && !ci.listInit && !ci.valueInit) copyFromOtherClass(d.type, args, d.pos);

    // **A derived object copied into a base whose copy is trivial is sliced**: `V v = q;` copies
    // q's V subobject, and with no copy constructor function to choose the bytes are the copy.
    if (!ci.listInit && !ci.valueInit && args.size() == 1 && args[0]->type() != nullptr &&
        isGlvalue(*args[0])) {
        const Type *argType = args[0]->type();
        const Type *to = d.type->unqualified(), *from = argType->unqualified();
        if (from != to && from->isStructOrUnion() && to->isStructOrUnion() &&
            copyConstructorOf(to) == nullptr && moveConstructorOf(to) == nullptr &&
            hasBaseClass(from, to)) {
            const Type *target = argType->isConst() ? types_.withConst(to) : to;
            ExprPtr addr(new Unary('&', std::move(args[0])));
            addr->setType(types_.pointerTo(argType));
            ExprPtr base(new Unary('*', convert(std::move(addr), types_.pointerTo(target))));
            base->setType(target);
            args[0] = std::move(base);
        }
    }

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
        const Type *cls = d.type->unqualified();
        if (sel != nullptr && sel->access != Access::Public && !insideAccessOf(cls, sel->access) &&
            !isFriendOf(cls))
            src_.fail(d.pos, "'" + cls->describe() + "' has a " +
                             (sel->access == Access::Private ? "private" : "protected") + " " +
                             (sel == mc ? "move" : "copy") + " constructor, and '" + d.name + " = ...' needs "
                             "it accessible however the copy is elided - [class.copy]/31");
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

    if (sameClass) refuseVolatileClassCopy(args[0]->type(), d.type, "'" + d.name + "'", d.pos);
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

// The init function's frame is entered the way every other re-entry door enters one, the state saved,
// cleared and the frame put in its place - holding the slots a namespace-scope initialiser read before
// this made its temporaries in: `int g = f(S(3));` wrote past it.
Parser::FunctionState Parser::enterInitFunction() {
    const int parsed = atFunctionBody_ ? 0 : frameSize_;
    FunctionState outer = captureFunctionState();
    clearFunctionState();
    frameSize_ = std::max(tlsObject_ ? tlsInitFrame_ : dynInitFrame_, parsed);
    functionName_ = tlsObject_ ? "__tls_init" : initFunctionSymbol();
    currentFunction_ = functionName_;
    atFunctionBody_ = true;
    initGuardMark_ = guardSlots_.size();
    return outer;
}

// The frame comes back out, and so do the guards made inside - they are not
// part of the state struct, so they are moved by hand. The token index is put
// back to where the initialiser ended, since the restore rewinds it.
void Parser::leaveInitFunction(const FunctionState &outer) {
    (tlsObject_ ? tlsInitFrame_ : dynInitFrame_) = frameSize_;
    for (std::size_t i = initGuardMark_; i < guardSlots_.size(); i++)
        (tlsObject_ ? tlsInitGuards_ : dynInitGuards_).push_back(guardSlots_[i]);
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

// **`__tls_init`, the per-thread twin of the function above**, measured from clang: Itanium runs it
// from a `_ZTH` alias the first time a thread reaches an object it built, under a thread_local guard
// byte of its own; the Microsoft CRT runs it once per thread from .CRT$XDU and keeps the guard itself.
void Parser::finishTlsInit(Program &program) {
    for (const std::string &s : tlsDefined_)
        program.tlsWrappers.push_back(TlsWrapper{ s, true, tlsDynamic_.count(s) != 0, false });
    for (const std::string &s : tlsReached_)
        if (tlsDefined_.count(s) == 0) program.tlsWrappers.push_back(TlsWrapper{ s, false, false, false });
    for (const std::string &s : tlsInternalDynamic_)
        program.tlsWrappers.push_back(TlsWrapper{ s, true, true, true });
    if (tlsInit_.empty()) return;
    std::vector<StmtPtr> body;
    for (std::size_t i = 0; i < tlsInitGuards_.size(); i++)
        body.push_back(StmtPtr(new ExprStmt(setGuard(tlsInitGuards_[i], 0))));
    if (target_.microsoftNames()) {
        for (std::size_t i = 0; i < tlsInit_.size(); i++) body.push_back(std::move(tlsInit_[i]));
    } else {
        const Type *byte = types_.get(Kind::UChar);
        program.globals.push_back(Global{ "__tls_guard", "__tls_guard", byte,
                                          std::vector<GlobalPiece>(), false, true, false });
        program.globals.back().isThreadLocal = true;
        auto guard = [&]() {
            Var *g = Var::global("__tls_guard");
            g->setSymbol("__tls_guard");
            tlsVar(g, false, false);
            ExprPtr e(g);
            e->setType(byte);
            return e;
        };
        ExprPtr zero(new Num(0LL));
        zero->setType(byte);
        ExprPtr fresh(new Binary(BinOp::Eq, guard(), std::move(zero)));
        fresh->setType(types_.get(Kind::Bool));
        ExprPtr one(new Num(1LL));
        one->setType(byte);
        ExprPtr mark(new Assign(guard(), std::move(one)));
        mark->setType(byte);
        std::vector<StmtPtr> once;
        once.push_back(StmtPtr(new ExprStmt(std::move(mark))));
        for (std::size_t i = 0; i < tlsInit_.size(); i++) once.push_back(std::move(tlsInit_[i]));
        body.push_back(StmtPtr(new If(std::move(fresh), StmtPtr(new Block(std::move(once))), StmtPtr())));
    }
    tlsInit_.clear();
    tlsInitGuards_.clear();
    program.functions.push_back(Function("__tls_init", types_.get(Kind::Void),
                                         std::vector<Param>(),
                                         StmtPtr(new Block(std::move(body))),
                                         alignTo(tlsInitFrame_, 16), true, 0,
                                         false, 0, 0, std::vector<::Local>()));
    program.functions.back().setSymbol("__tls_init");
    program.tlsInitFunction = "__tls_init";
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
    // The named return value: its slot holds the caller's address, and the object is what that points at.
    if (symbol.empty() && offset != 0 && offset == nrvoSlot_ && d.name == nrvoName_) {
        ExprPtr held(Var::local(d.name, offset));
        held->setType(types_.pointerTo(d.type->unqualified()));
        ExprPtr at(new Unary('*', std::move(held)));
        at->setType(d.type);
        return at;
    }
    Var *v = symbol.empty() ? Var::local(d.name, offset) : Var::global(d.name);
    if (!symbol.empty()) v->setSymbol(symbol);
    if (!symbol.empty() && tlsObject_) tlsVar(v, false, false);
    ExprPtr e(v);
    e->setType(d.type);
    if (symbol.empty() || offset == 0) return e;
    // An element of a global array: `*(T *)((char *)&g + offset)`, the add in bytes.
    const Type *chars = types_.pointerTo(types_.get(Kind::Char));
    const Type *ptr = types_.pointerTo(d.type);
    ExprPtr addr(new Unary('&', std::move(e)));
    addr->setType(ptr);
    ExprPtr asChars(new Cast(chars, std::move(addr)));
    asChars->setType(chars);
    ExprPtr step(new Num(static_cast<long long>(offset)));
    step->setType(types_.get(Kind::LongLong));
    ExprPtr moved(new Binary(BinOp::Add, std::move(asChars), std::move(step)));
    moved->setType(chars);
    ExprPtr back(new Cast(ptr, std::move(moved)));
    back->setType(ptr);
    ExprPtr there(new Unary('*', std::move(back)));
    there->setType(d.type);
    return there;
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
    // No constructor to call and no initialiser: the object is its zero-filled storage, and only its
    // destructor is registered.
    if (d.type->isStructOrUnion() && !d.type->tag().empty() && !hasConstructors(d.type->tag()) &&
        !peek().is("=") && !peek().is("(") && !peek().is("{")) {
        std::vector<StmtPtr> only;
        registerDestruction(d, symbol, helper, only);
        return only;
    }
    if (isInitializerListType(d.type)) staticIlFor_ = symbol;
    CtorInit ci;
    try {
        ci = readConstructorInitialiser(d);
    } catch (...) {
        staticIlFor_.clear();
        throw;
    }
    staticIlFor_.clear();
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
std::vector<StmtPtr> Parser::buildStaticArrayConstruction(Declared &d,
                                                          const std::string &symbol,
                                                          const std::string &helper) {
    const Type *elem = d.type;
    while (elem->isArray()) elem = elem->pointee();
    const Type *plain = elem->unqualified();
    const Type *ptr = types_.pointerTo(plain);
    const Type *sizeT = types_.get(target_.sizeType());
    if (peek().is("("))
        src_.fail(d.pos, "an array is initialised from a braced list - "
                         "write '" + d.name + "[...] = { ... }'");
    const bool listed = peek().is("{") || (peek().is("=") && peekAt(1).is("{"));
    if (peek().is("=") && !listed)
        src_.fail(d.pos, "an array of '" + plain->describe() + "' is "
                         "initialised from a braced list, '= { ... }'");
    if (listed && !hasConstructors(plain->tag()))
        src_.fail(d.pos, "a braced list for an array of '" + plain->describe() +
                         "', a class with a destructor and no constructor, is "
                         "not supported yet");
    if (!listed && d.type->length() < 0)
        src_.fail(d.pos, "'" + d.name + "' has no length and no initialiser "
                         "to take one from");
    // `S g[] = {...}`: each element copy-initialised from its initialiser, as a local's are.
    std::vector<StmtPtr> out;
    if (listed) {
        consume("=");
        out.push_back(constructLocalArrayFromList(d, 0, [] { return 0; }, symbol));
    }
    long long count = 1;
    for (const Type *t = d.type; t->isArray(); t = t->pointee()) count *= t->length();

    // The array's address, which is its first element's.
    auto arrayAddress = [&]() {
        ExprPtr addr(new Unary('&', objectAt(d, symbol, 0)));
        addr->setType(types_.pointerTo(d.type));
        ExprPtr first(new Cast(ptr, std::move(addr)));
        first->setType(ptr);
        return first;
    };
    if (!listed && hasConstructors(plain->tag())) {
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
    const FrameSave savedFrame = openFrame();
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
    closeFrame(savedFrame);

    std::vector<ExprPtr> args;
    if (ms) {
        const Type *helperType = types_.functionType(types_.get(Kind::Void),
                                                     std::vector<const Type *>(), false);
        args.push_back(functionAddress(fn, helperType));
        out.push_back(StmtPtr(new ExprStmt(runtimeCall(tlsObject_ ? target_.threadAtexit() : "atexit",
                                                       types_.intType(), std::move(args)))));
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
    out.push_back(StmtPtr(new ExprStmt(runtimeCall(tlsObject_ ? target_.threadAtexit() : "__cxa_atexit",
                                                   types_.intType(), std::move(args)))));
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

        // __cxa_thread_atexit and _tlv_atexit take the same three arguments, measured from clang.
        into.push_back(StmtPtr(new ExprStmt(
            runtimeCall(tlsObject_ ? target_.threadAtexit() : "__cxa_atexit",
                        types_.intType(), std::move(args)))));
        return;
    }

    // The helper: no parameters, one destructor call, file-local. Built in a
    // frame of its own, as the implicit special members are.
    const FrameSave savedFrame = openFrame();
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
    closeFrame(savedFrame);

    const Type *helperType = types_.functionType(types_.get(Kind::Void),
                                                 std::vector<const Type *>(),
                                                 false);
    std::vector<ExprPtr> args;
    args.push_back(functionAddress(helper, helperType));
    into.push_back(StmtPtr(new ExprStmt(
        runtimeCall(tlsObject_ ? target_.threadAtexit() : "atexit", types_.intType(), std::move(args)))));
}

// A file-scope object with a constructor, or a static data member of one,
// built inside the init function. `once` is a template's static member: a
// weak guard beside it keeps a second unit's copy from building it again.
void Parser::dynamicInitialise(Declared &d, const std::string &symbol,
                               const std::string &helper, bool once, std::vector<GlobalPiece> *folded) {
    if (d.type->isConst() && !peek().is("=") && !peek().is("(") &&
        !peek().is("{"))
        requireConstInitialised(d.type, d.name, d.pos);
    // A static member's constructor arguments keep its class's scope.
    const Type *scope = currentClass_;
    const bool scopeStatic = inStaticMember_;
    const FunctionState outer = enterInitFunction();
    currentClass_ = scope;
    inStaticMember_ = scopeStatic;
    std::vector<StmtPtr> built = d.type->isArray()
        ? buildStaticArrayConstruction(d, symbol, helper)
        : buildStaticConstruction(d, symbol, helper);
    // **A `constexpr` object is folded from these statements and laid down as data**, [basic.start.init]/2.
    if (folded != nullptr) {
        std::vector<const Stmt *> steps;
        for (const StmtPtr &b : built) steps.push_back(b.get());
        const std::unique_ptr<Var> self(Var::global(d.name));
        self->setSymbol(symbol);
        ConstObject value;
        if (d.type->isArray() || !foldDeclaredObject(steps, *self, &value, d.pos))
            refuseConstexprClassObject(d.name, d.pos);
        constObjects_[symbol] = value;
        constObjectPieces(value, *folded);
        leaveInitFunction(outer);
        return;
    }
    if (once) {
        StmtPtr guarded = guardTemplateMember(symbol, std::move(built));
        built.clear();
        built.push_back(std::move(guarded));
    }
    for (std::size_t i = 0; i < built.size(); i++)
        initSink().push_back(std::move(built[i]));
    leaveInitFunction(outer);
}

// The shapes flattenInit lays down as bytes. A class object here has no
// constructor - that path was taken earlier - so `P g = P();` and `P g = f();`
// are trivial copies made at run time; a braced list stays with flattenInit.
bool Parser::staticallyInitialisable(const Type *t, Init &in) {
    if (needsConstruction(t)) return false;
    if (in.value == nullptr) return true;
    if (in.isList)
        return in.items.size() != 1 || in.items[0].isList || t->isArray() ||
               t->isStructOrUnion() || staticallyInitialisable(t, in.items[0]);
    if (t->isArray()) return true;
    if (t->isStructOrUnion()) return false;
    in.value = decay(std::move(in.value));
    if (t->isFloating()) { long double d; return foldFloating(*in.value, &d); }
    long long v;
    if (t->isPointer()) {
        std::string sym;
        if (foldAddress(*in.value, &sym, &v)) return true;
    }
    return fold(*in.value, &v, in.pos);
}

void Parser::dynamicInitialiseScalar(const std::string &name, const Type *type,
                                     Init &in) {
    const FunctionState outer = enterInitFunction();
    std::vector<InitStep> path;
    emitInit(name, path, type, in, initSink());
    flushTemporaries(initSink());
    leaveInitFunction(outer);
}

StmtPtr Parser::guardTemplateMember(const std::string &symbol,
                                    std::vector<StmtPtr> built) {
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
    return StmtPtr(new If(std::move(fresh), StmtPtr(new Block(std::move(body))),
                          StmtPtr()));
}

void Parser::dynamicInitialiseStaticMember(const std::string &name,
                                           const std::string &symbol,
                                           const Type *type, Init &in, bool once) {
    const FunctionState outer = enterInitFunction();
    std::vector<StmtPtr> built;
    Var *v = Var::global(name);
    v->setSymbol(symbol);
    if (tlsObject_) tlsVar(v, false, false);
    ExprPtr target(v);
    target->setType(type);
    checkAssignable(*in.value, type, in.pos, "'" + name + "'");
    ExprPtr a(new Assign(std::move(target), convert(decay(std::move(in.value)), type)));
    a->setType(type);
    built.push_back(StmtPtr(new ExprStmt(std::move(a))));
    flushTemporaries(built);
    if (once) {
        StmtPtr guarded = guardTemplateMember(symbol, std::move(built));
        built.clear();
        built.push_back(std::move(guarded));
    }
    for (std::size_t i = 0; i < built.size(); i++)
        initSink().push_back(std::move(built[i]));
    leaveInitFunction(outer);
}

// **A block-scope thread_local built once per thread** - [stmt.dcl]/4 - needs no lock: the
// guard is a thread_local byte of its own, set after the construction so that one which throws
// is tried again. Measured from clang, which writes `_ZGV<name>` in .tbss and no __cxa_guard call.
StmtPtr Parser::guardOnceThread(const std::string &symbol, std::vector<StmtPtr> body) {
    const std::string guard = symbol + (target_.microsoftNames() ? "$tguard" : ".tguard");
    const Type *byte = types_.get(Kind::UChar);
    current_->globals.push_back(Global{ guard, guard, byte, std::vector<GlobalPiece>(), false, true, false });
    current_->globals.back().isThreadLocal = true;
    auto flag = [&]() {
        Var *g = Var::global(guard);
        g->setSymbol(guard);
        tlsVar(g, false, false);
        ExprPtr e(g);
        e->setType(byte);
        return e;
    };
    ExprPtr zero(new Num(0LL));
    zero->setType(byte);
    ExprPtr fresh(new Binary(BinOp::Eq, flag(), std::move(zero)));
    fresh->setType(types_.get(Kind::Bool));
    ExprPtr one(new Num(1LL));
    one->setType(byte);
    ExprPtr set(new Assign(flag(), std::move(one)));
    set->setType(byte);
    body.push_back(StmtPtr(new ExprStmt(std::move(set))));
    return StmtPtr(new If(std::move(fresh), StmtPtr(new Block(std::move(body))), StmtPtr()));
}

// [stmt.dcl]/4 under the ABI's own guard: __cxa_guard_acquire / _release
// around the construction on Itanium; _Init_thread_header / _footer with the
// guard at -1 on Microsoft, without cl's TLS epoch fast path - CONFORMANCE.md.
StmtPtr Parser::guardOnce(const std::string &symbol, std::vector<StmtPtr> body) {
    if (tlsObject_) return guardOnceThread(symbol, std::move(body));
    const bool ms = target_.microsoftNames();
    std::string guard = symbol + (ms ? "$guard" : ".guard");
    // **An inline function's guard is shared as its object is**, under the ABI's name: cl's
    // `?$TSS0@?1??f@@YAHXZ@4HA`, counting up through the function, with the object's block digit;
    // Itanium's `_ZGVZ1fvE1s` (1.28, 2026-10-22, review of 1.25, item 14).
    const bool shared = sharedStaticLocal(symbol);
    if (shared) {
        if (ms) {
            const std::string &owner = currentFunction_.empty() ? functionName_ : currentFunction_;
            guard = "?$TSS" + std::to_string(tssSeq_++) + "@?" +
                    std::to_string(currentBlock() + 1) + "?" + owner + "@4HA";
        } else {
            guard = "_ZGV" + symbol.substr(2);
        }
    }
    const Type *guardType = types_.get(ms ? Kind::Int : Kind::LongLong);
    current_->globals.push_back(Global{ guard, guard, guardType,
                                        std::vector<GlobalPiece>(), false,
                                        !shared, false });
    current_->globals.back().isInline = shared;
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

    // **A throwing initialiser leaves the guard `being built`**: the build
    // runs under a cleanup that aborts it (__cxa_guard_abort, [stmt.dcl]/4),
    // release and footer outside; not in a Microsoft handler (see block()).
    auto guarded = [&](const char *abort) {
        std::vector<ExprPtr> a;
        a.push_back(guardAddr());
        StmtPtr onUnwind(new ExprStmt(runtimeCall(abort, types_.get(Kind::Void), std::move(a))));
        std::vector<StmtPtr> region;
        region.push_back(cleanupRegion(std::move(body), std::move(onUnwind)));
        return region;
    };
    if (!ms) {
        std::vector<ExprPtr> a1;
        a1.push_back(guardAddr());
        ExprPtr acquired = runtimeCall("__cxa_guard_acquire", types_.intType(),
                                       std::move(a1));
        std::vector<ExprPtr> a2;
        a2.push_back(guardAddr());
        std::vector<StmtPtr> built = guarded("__cxa_guard_abort");
        built.push_back(StmtPtr(new ExprStmt(
            runtimeCall("__cxa_guard_release", types_.get(Kind::Void),
                        std::move(a2)))));
        std::vector<StmtPtr> inner;
        inner.push_back(StmtPtr(new If(std::move(acquired),
                                       StmtPtr(new Block(std::move(built))),
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
    std::vector<StmtPtr> built = inHandlerBody_ ? std::move(body) : guarded("_Init_thread_abort");
    built.push_back(StmtPtr(new ExprStmt(
        runtimeCall("_Init_thread_footer", types_.get(Kind::Void),
                    std::move(a2)))));
    whole.push_back(StmtPtr(new If(guardIs(-1, Kind::Int),
                                   StmtPtr(new Block(std::move(built))),
                                   StmtPtr())));
    return StmtPtr(new Block(std::move(whole)));
}

// A static local's symbol: the function's mangled symbol and the name; an
// inline function's is one object, [basic.def.odr]/6, with the ABI's name in a
// COMDAT (`?a@?1??f@@YAHXZ@4HA`, `_ZZ1fvE1a`); without a type, per unit.
std::string Parser::uniqueStaticSymbol(const std::string &name, const Type *type) {
    const std::string &owner = currentFunction_.empty() ? functionName_
                                                        : currentFunction_;
    if (currentFunctionInline_ && type != nullptr) {
        std::string abi;
        if (target_.microsoftNames()) {
            std::string data, why;
            const std::string head = "?" + name + "@@3";
            if (!owner.empty() && owner[0] == '?' && microsoftDataName(name, type, &data, &why) &&
                data.compare(0, head.size(), head) == 0)
                abi = "?" + name + "@?" + std::to_string(currentBlock() + 1) + "?" + owner + "@4" +
                      data.substr(head.size());
        } else if (owner.compare(0, 2, "_Z") == 0) {
            abi = "_ZZ" + owner.substr(2) + "E" + std::to_string(name.size()) + name;
            int seen = 0;
            for (const std::string &used : staticSymbols_)
                if (used == abi || used.compare(0, abi.size() + 1, abi + "_") == 0) seen++;
            if (seen > 0) abi += "_" + std::to_string(seen - 1);
        }
        if (!abi.empty()) {
            staticSymbols_.push_back(abi);
            return abi;
        }
    }
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
void Parser::staticLocalWithConstructor(Declared &d,
                                        std::vector<StmtPtr> &inits) {
    const std::string symbol = uniqueStaticSymbol(d.name, tlsObject_ ? nullptr : d.type);
    std::string helper;
    if (target_.microsoftNames()) {
        const std::string owner =
            !currentFunction_.empty() && currentFunction_[0] == '?'
                ? currentFunction_ : "?" + currentFunction_ + "@@9";
        helper = atexitHelperName(d.name + "@?1?" + owner);
    }
    const bool early = initialiserNames(d.name);
    if (early) declareStaticLocal(d.name, d.type, d.pos, symbol);
    const std::size_t mine = locals_.size() - 1;
    std::vector<StmtPtr> body = d.type->isArray()
        ? buildStaticArrayConstruction(d, symbol, helper)
        : buildStaticConstruction(d, symbol, helper);
    if (!early) declareStaticLocal(d.name, d.type, d.pos, symbol);
    locals_[early ? mine : locals_.size() - 1].isConst = d.type->isConst();
    markThreadLocal(early ? mine : locals_.size() - 1, tlsObject_);
    // Not `isConst`: the constructor writes it, so it cannot live in .rodata.
    current_->globals.push_back(Global{ symbol, symbol, d.type,
                                        std::vector<GlobalPiece>(), false,
                                        !sharedStaticLocal(symbol), false });
    current_->globals.back().isInline = sharedStaticLocal(symbol);
    current_->globals.back().isThreadLocal = tlsObject_;
    inits.push_back(guardOnce(symbol, std::move(body)));
}

// A reference with static storage duration: `&global` goes into the image,
// anything else is bound where `into` says - the init function, or a static
// local's guarded statement. A temporary is refused by name.
void Parser::bindStaticReference(const Declared &d, const std::string &symbol,
                                 std::vector<GlobalPiece> &pieces, bool &hasInit,
                                 std::vector<StmtPtr> *into) {
    // `int &g(x);`, [dcl.init]/16's other spelling (1.79).
    const bool paren = peek().is("(") && atParenInitialiser();
    // `const int &g = {3};`, `int &&g{4};` - [dcl.init.list]/3: the list's one element, for a referent that is
    // no class and no array (1.80: at namespace scope and for a static, where a block's had been read).
    const bool braced = (peek().is("{") || (peek().is("=") && peekAt(1).is("{"))) &&
                        !d.type->referent()->unqualified()->isStructOrUnion() && !d.type->referent()->isArray();
    if (!peek().is("=") && !paren && !braced)
        src_.fail(d.pos, "'" + d.name + "' is a reference and has to be "
                         "initialised here - there is no later assignment "
                         "that would bind it, only one that writes through it");
    ExprPtr init;
    if (braced) {
        consume("=");
        const std::size_t bpos = peek().pos;
        OverloadTarget hold(*this, d.type);
        Init in = parseInitialiser();
        if (in.items.size() != 1 || in.items[0].isList)
            src_.fail(bpos, "'" + d.name + "' is a reference, and a braced initialiser binds it "
                            "to one value - this list has " + std::to_string(in.items.size()));
        init = std::move(in.items[0].value);
    } else {
        at_++;
        OverloadTarget hold(*this, d.type);
        init = assign();
    }
    if (paren) expect(")");
    // [class.temporary]/5: a static reference's temporary is a static object
    // of its own (clang's `_ZGR`, cl's `?$S`): a scalar laid down or stored
    // before main, a class built at `into`, destroyed at exit in order.
    const Type *held = d.type->referent()->unqualified();
    const std::string tmp = target_.microsoftNames()
        ? "?$RT" + std::to_string(staticTemps_ + 1) + "@" + symbol
        : symbol.compare(0, 2, "_Z") == 0 ? "_ZGR" + symbol.substr(2) + "_"
                                           : "_ZGR" + std::to_string(symbol.size()) + symbol + "_";
    if ((!isGlvalue(*init) || init->declCategory() == Expr::Category::Prvalue) && init->type() != nullptr &&
        init->type()->unqualified()->isStructOrUnion() && !tlsObject_) {
        staticTemps_++;
        const Type *made = init->type()->unqualified();
        Declared td = d;
        td.name = tmp;
        td.type = made;
        Var *at = Var::global(tmp);
        at->setSymbol(tmp);
        ExprPtr where(at);
        where->setType(made);
        ExprPtr addr(new Unary('&', std::move(where)));
        addr->setType(types_.pointerTo(made));
        if (constructInPlace(*init, made, addr)) {
            into->push_back(StmtPtr(new ExprStmt(std::move(init))));
        } else {
            // A call's result, say, is made in the frame; moving it out is a copy nobody wrote, unless a trivial one.
            if (!trivialSpecial(made, kCopyCtor) || !trivialSpecial(made, kDestructor))
                src_.fail(d.pos, "'" + d.name + "' is a reference with static storage duration, and the '" +
                                 made->describe() + "' temporary it binds is made where only a copy could keep it - "
                                 "that is not supported yet. Name the object and bind to that");
            ExprPtr store(new Assign(objectAt(td, tmp, 0), std::move(init)));
            store->setType(made);
            into->push_back(StmtPtr(new ExprStmt(std::move(store))));
        }
        flushTemporaries(*into);
        registerDestruction(td, tmp, atexitHelperName(tmp), *into);
        current_->globals.push_back(Global{ tmp, tmp, made, std::vector<GlobalPiece>(), false, true, false });
        Var *named = Var::global(tmp);
        named->setSymbol(tmp);
        init.reset(named);
        init->setType(made);
        init->setXvalue();
    }
    if (!isGlvalue(*init) && held->isScalar() && !tlsObject_) {
        staticTemps_++;
        ExprPtr value = convert(decay(std::move(init)), held);
        std::vector<GlobalPiece> made;
        long long v = 0;
        bool constant = held->isInteger() && fold(*value, &v, d.pos);
        if (constant) {
            GlobalPiece p;
            p.offset = 0;
            p.size = held->size(target_);
            p.value = v;
            made.push_back(p);
        } else {
            Var *at = Var::global(tmp);
            at->setSymbol(tmp);
            ExprPtr lhs(at);
            lhs->setType(held);
            ExprPtr store(new Assign(std::move(lhs), std::move(value)));
            store->setType(held);
            into->push_back(StmtPtr(new ExprStmt(std::move(store))));
            flushTemporaries(*into);
        }
        current_->globals.push_back(Global{ tmp, tmp, held, made, constant, true,
                                            constant && d.type->referent()->isConst() });
        GlobalPiece p;
        p.offset = 0;
        p.size = types_.pointerTo(held)->size(target_);
        p.value = 0;
        p.symbol = tmp;
        pieces.push_back(p);
        hasInit = true;
        return;
    }
    if (!isGlvalue(*init))
        src_.fail(d.pos, "'" + d.name + "' is a reference with static storage "
                         "duration, and binding it to a class temporary is not "
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
    if (tlsObject_) tlsVar(target, false, false);
    ExprPtr t(target);
    t->setType(slot);
    ExprPtr bind(new Assign(std::move(t), std::move(addr)));
    bind->setType(slot);
    into->push_back(StmtPtr(new ExprStmt(std::move(bind))));
    flushTemporaries(*into);
}
