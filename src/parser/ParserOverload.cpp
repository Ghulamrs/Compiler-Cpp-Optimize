// The parser: conversions and overload resolution. The implicit conversion
// sequences of [conv] ranked as [over.ics] ranks them, the tournament that
// ranking decides, and the assignability check that asks the same questions.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <algorithm>
#include <climits>
#include <cstring>

const Type *Parser::unsignedVersion(const Type *t) const {
    switch (t->kind()) {
    case Kind::Int:      return types_.get(Kind::UInt);
    case Kind::Long:     return types_.get(Kind::ULong);
    case Kind::LongLong: return types_.get(Kind::ULongLong);
    default:             return t;
    }
}

const Type *Parser::promote(const Type *t) const {
    // [conv.prom]/3: an unscoped enumeration promotes as its underlying integer does; a scoped one not at all.
    if (t->isScopedEnumeration()) return t;
    if (t->isEnumeration()) t = types_.get(t->kind());
    if (t->isInteger() && t->rank() < types_.intType()->rank())
        return types_.intType();
    // [conv.prom]/2: char32_t goes to the first of int, unsigned int that holds it.
    if (t->kind() == Kind::Char32) return types_.get(Kind::UInt);
    return t;
}

const Type *Parser::usualArithmetic(const Type *a, const Type *b) const {
    // [expr]/10 starts from an unscoped enumeration: two of one scoped type compare, nothing else mixes.
    if (a->isScopedEnumeration() || b->isScopedEnumeration()) {
        if (a->unqualified() == b->unqualified()) return a->unqualified();
        refuseScopedConversion(a, b, peek().pos);
    }
    if (a->kind() == Kind::LongDouble || b->kind() == Kind::LongDouble)
        return types_.get(Kind::LongDouble);
    if (a->kind() == Kind::Double || b->kind() == Kind::Double)
        return types_.doubleType();
    if (a->kind() == Kind::Float || b->kind() == Kind::Float)
        return types_.get(Kind::Float);

    a = promote(a);
    b = promote(b);
    if (a == b) return a;

    bool as = a->isSigned(target_), bs = b->isSigned(target_);
    const Type *hi = a->rank() >= b->rank() ? a : b;
    if (as == bs) return hi;

    const Type *uns = as ? b : a;
    const Type *sig = as ? a : b;
    if (uns->rank() >= sig->rank()) return uns;
    if (sig->size(target_) > uns->size(target_)) return sig;
    return unsignedVersion(sig);
}

// Defined below, beside the conversion rules it belongs with.
int publicBaseOffset(const Type *derived, const Type *base);

static bool publiclyDerivedFrom(const Type *derived, const Type *base);
static int anyBaseOffset(const Type *derived, const Type *base);

// [dcl.enum]/10: what makes `enum class` worth writing, said where the conversion would be made.
void Parser::refuseScopedConversion(const Type *from, const Type *to, std::size_t pos) const {
    auto shown = [](const Type *t) {
        const Type *u = t->unqualified();
        if (u->isScopedEnumeration()) return "enum class " + u->enumTag();
        return u->isEnumeration() ? "enum " + u->enumTag() : t->describe();
    };
    src_.fail(pos, "'" + shown(from) + "' does not convert to '" + shown(to) + "' implicitly - a "
                   "scoped enumeration converts only with a cast, [dcl.enum]/10");
}

// The built-in arithmetic and bitwise operators take no scoped enumeration, [expr]/10.
void Parser::refuseScopedOperand(const Type *a, const Type *b, const char *op, std::size_t pos) const {
    const Type *t = a != nullptr && a->unqualified()->isScopedEnumeration() ? a
                  : b != nullptr && b->unqualified()->isScopedEnumeration() ? b : nullptr;
    if (t == nullptr) return;
    src_.fail(pos, "'" + std::string(op) + "' does not take 'enum class " + t->unqualified()->enumTag() +
                   "' - a scoped enumeration has no arithmetic; '==' and '<' compare two of one "
                   "type, and a cast reaches the integer");
}

// Whether `to` is a base of `from` along non-virtual derivations only.
static bool nonVirtualBasePath(const Type *from, const Type *to) {
    for (const Type::BaseSpec &b : from->unqualified()->bases()) {
        if (b.isVirtual) continue;
        if (b.type->unqualified() == to || nonVirtualBasePath(b.type, to)) return true;
    }
    return false;
}

// Every virtual base of `cls`, its own and its bases'.
static void virtualBasesOf(const Type *cls, std::vector<const Type *> *out) {
    for (const Type::BaseSpec &b : cls->unqualified()->bases()) {
        const Type *t = b.type->unqualified();
        if (b.isVirtual && std::find(out->begin(), out->end(), t) == out->end()) out->push_back(t);
        virtualBasesOf(t, out);
    }
}

ExprPtr Parser::baseSubobject(ExprPtr e, const Type *base) const {
    const Type *from = e->type();
    const Type *want = from->isConst() ? types_.withConst(base->unqualified()) : base->unqualified();
    ExprPtr at(new Unary('&', std::move(e)));
    at->setType(types_.pointerTo(from));
    at = convert(std::move(at), types_.pointerTo(want), true);
    ExprPtr sub(new Unary('*', std::move(at)));
    sub->setType(want);
    return sub;
}

ExprPtr Parser::convert(ExprPtr e, const Type *to, bool allowExplicit) const {
    if (e->type() == to) return e;
    // A scoped enumeration converts only by a cast, in either direction - [dcl.enum]/10, [expr.static.cast]/9.
    if (!allowExplicit && e->type() != nullptr && e->type()->unqualified() != to->unqualified() &&
        (e->type()->isScopedEnumeration() || to->isScopedEnumeration()) && !to->isReference())
        refuseScopedConversion(e->type(), to, peek().pos);

    // **A class converted by its own conversion function**, which is where a
    // class reaches a number, a pointer or a bool.
    if (e->type() != nullptr && e->type()->unqualified()->isStructOrUnion() &&
        !to->unqualified()->isStructOrUnion()) {
        Parser *self = const_cast<Parser *>(this);
        if (const Signature *how = self->conversionFunction(e->type(), to, allowExplicit)) {
            const Type *object = e->type();
            std::vector<ExprPtr> none;
            ExprPtr got = self->memberCallWith(std::move(e), object, how->name,
                                               0, std::move(none));
            return got->type() == to ? std::move(got)
                                     : convert(std::move(got), to);
        }
    }

    // **A class converted to another class by its own conversion function**,
    // [class.conv.fct] - the same one rule as above, only the target is a
    // class too: `operator B()` on an A makes a B.
    if (e->type() != nullptr && e->type()->unqualified()->isStructOrUnion() &&
        to->unqualified()->isStructOrUnion() &&
        e->type()->unqualified() != to->unqualified() &&
        anyBaseOffset(e->type()->unqualified(), to->unqualified()) < 0) {
        Parser *self = const_cast<Parser *>(this);
        const Signature *found = self->conversionFunction(e->type(), to, allowExplicit);
        std::string how = found != nullptr ? found->name : std::string();
        // [over.match.copy]/1: in a copy-initialisation the target's constructor from this class is a
        // candidate beside the conversion function - `const A &r = b;`, an aggregate's member from a b. Where
        // the two were compared and the constructor stays, it is the one below (1.80: the function was taken
        // unasked, and two equally good were not refused here as they are for an argument).
        if (found != nullptr && !allowExplicit)
            if (const Signature *ctor = self->convertingConstructor(to, *e, 0, false)) {
                bool compared = false;
                if (self->conversionOverConstructor(to->unqualified(), *e, *ctor, peek().pos, &compared) == nullptr &&
                    compared)
                    how.clear();
            }
        if (!how.empty()) {
            const Type *object = e->type();
            std::vector<ExprPtr> none;
            return self->memberCallWith(std::move(e), object, how,
                                        0, std::move(none));
        }
    }

    // **A class made by one of its converting constructors**, [over.match.ctor]: `(CMatrix<3,3>)v`
    // and `static_cast<S>(2.5)` are direct-initialisations, so an explicit one is taken too.
    if (e->type() != nullptr && to->unqualified()->isStructOrUnion() &&
        e->type()->unqualified() != to->unqualified() &&
        !(e->type()->unqualified()->isStructOrUnion() &&
          anyBaseOffset(e->type()->unqualified(), to->unqualified()) >= 0)) {
        Parser *self = const_cast<Parser *>(this);
        if (const Signature *ctor = self->convertingConstructor(to, *e, 0, allowExplicit)) {
            self->markUsed(ctor);
            std::vector<ExprPtr> one;
            one.push_back(std::move(e));
            self->applyDefaults(*ctor, one, 0);
            return self->constructTemporary(to->unqualified(), *ctor, std::move(one), false, 0);
        }
    }

    // **A class differing only in const-ness is the same object**, so there is
    // nothing here to convert: [conv.qual] changes the type and not the value,
    // and the object keeps its address and its bytes.
    if (to->unqualified() == e->type()->unqualified() &&
        to->unqualified()->isStructOrUnion()) {
        e->setType(to);
        return e;
    }

    // **A derived object copied to a base with no copy constructor of its own is its base subobject**,
    // [class.copy]/12 - slicing, `T t = u;` and `w = u`: the copy is of bytes, so it is a load of
    // the subobject. A prvalue is kept in a frame slot first, as a reference's temporary is.
    Parser *self = const_cast<Parser *>(this);
    if (e->type() != nullptr && e->type()->unqualified()->isStructOrUnion() &&
        to->unqualified()->isStructOrUnion() && e->type()->unqualified() != to->unqualified() &&
        anyBaseOffset(e->type()->unqualified(), to->unqualified()) >= 0 &&
        self->copyConstructorOf(to->unqualified()) == nullptr &&
        self->moveConstructorOf(to->unqualified()) == nullptr) {
        const Type *from = e->type();
        ExprPtr keep;
        if (!isGlvalue(*e)) {
            const int slot = self->allocateFrameSlot(from->unqualified());
            const std::string temp = ".sl" + std::to_string(self->refTemps_++);
            ExprPtr target(Var::local(temp, slot));
            target->setType(from->unqualified());
            keep.reset(new Assign(std::move(target), std::move(e)));
            keep->setType(from->unqualified());
            e.reset(Var::local(temp, slot));
            e->setType(from->unqualified());
            from = from->unqualified();
        }
        ExprPtr base = baseSubobject(std::move(e), to->unqualified());
        if (keep == nullptr) return base;
        const Type *baseType = base->type();
        ExprPtr both(new Comma(std::move(keep), std::move(base)));
        both->setType(baseType);
        return both;
    }
    // **A null data member pointer is -1 on both ABIs** (0 is a real member;
    // Microsoft's virtual model writes {0, -1}). nullptr or 0 as a member
    // function pointer: the pair with a null code word, [conv.mem]/1.
    const bool nullConstant = e->type()->isNullPtr() || (e->type()->isInteger() && isNullConstant(*e));
    if (nullConstant && to->unqualified()->isMemberFunctionPointer())
        return const_cast<Parser *>(this)->nullMemberFunctionPointer(to->unqualified());
    if (nullConstant && to->unqualified()->isMemberPointer()) {
        const bool virtualModel = target_.microsoftNames() && to->unqualified()->size(target_) == 8;
        ExprPtr n(new Num(virtualModel ? -4294967296LL : -1LL));
        n->setType(types_.get(Kind::LongLong));
        ExprPtr asTo(new Cast(to->unqualified(), std::move(n)));
        asTo->setType(to);
        return asTo;
    }
    {
        // A member pointer of a base becoming one of the derived class: the
        // value moves by the base's offset, or the pair is rebuilt where the
        // two classes' models differ in width; at 0 and one width, only the type.
        const int shift = memberPointerToDerived(e->type(), to);
        const Type *ft = e->type()->unqualified();
        if (shift >= 0 && ft->enclosing()->unqualified() != to->unqualified()->enclosing()->unqualified()) {
            if (shift > 0 || ft->size(target_) != to->unqualified()->size(target_))
                return convertMemberPointer(std::move(e), to->unqualified(), shift);
            e->setType(to);
            return e;
        }
    }

    // **Derived * to Base * moves the value where the base is not the first one**
    // - B at 4 walks the pointer forward by four. The null check is the rule and
    // not caution: a null pointer converts to one, and `(char *)0 + 4` is not null.
    if (to->isPointer() && e->type()->isPointer() &&
        to->pointee()->isStructOrUnion() && e->type()->pointee()->isStructOrUnion()) {
        // **A base of a virtual base** - ios_base in an ostringstream - is reached through that virtual base, whose
        // place the object's table gives: a class derived from the one naming the virtual base lays it out elsewhere,
        // and the offset the base's own layout gives points into the derived class's members.
        {
            const Type *s = e->type()->pointee()->unqualified(), *d = to->pointee()->unqualified();
            std::vector<const Type *> vbases;
            if (s != d && !nonVirtualBasePath(s, d)) virtualBasesOf(s, &vbases);
            if (std::find(vbases.begin(), vbases.end(), d) == vbases.end())
                for (const Type *v : vbases)
                    if (anyBaseOffset(v, d) >= 0) {
                        const Type *via = e->type()->pointee()->isConst() ? types_.withConst(v) : v;
                        ExprPtr mid = convert(std::move(e), types_.pointerTo(via), allowExplicit);
                        return convert(std::move(mid), to, allowExplicit);
                    }
        }
        const int off = anyBaseOffset(e->type()->pointee(), to->pointee());
        // **A virtual base is not at a constant offset from the derived
        // pointer**, so the walk forward is a number read out of the vtable
        // rather than one written here.
        const Type *srcCls = e->type()->pointee()->unqualified();
        const Type *dstCls = to->pointee()->unqualified();
        const long long vbBack = target_.microsoftNames()
                               ? 0 : itaniumVbaseOffsetSlot(srcCls, dstCls);
        // **The same walk on the Microsoft ABI, through the vbtable.**
        if (target_.microsoftNames() && srcCls->vbptrOffset() >= 0 &&
            virtualBaseSlot(srcCls, dstCls, nullptr) > 0) {
            Parser *self = const_cast<Parser *>(this);
            const Type *chars = types_.pointerTo(types_.get(Kind::Char));
            ExprPtr asChars(new Cast(chars, std::move(e)));
            asChars->setType(chars);
            const int slot = self->allocateFrameSlot(chars);
            const std::string temp = ".vp" + std::to_string(self->refTemps_++);
            ExprPtr held(Var::local(temp, slot));
            held->setType(chars);
            ExprPtr save(new Assign(std::move(held), std::move(asChars)));
            save->setType(chars);

            ExprPtr step = microsoftVirtualBaseStep(srcCls, dstCls, temp, slot,
                                                    nullptr);
            ExprPtr from(Var::local(temp, slot));
            from->setType(chars);
            ExprPtr moved(new Binary(BinOp::Add, std::move(from),
                                     std::move(step)));
            moved->setType(chars);
            ExprPtr asTo(new Cast(to, std::move(moved)));
            asTo->setType(to);
            ExprPtr whole(new Comma(std::move(save), std::move(asTo)));
            whole->setType(to);
            return whole;
        }
        if (vbBack != 0 && !target_.microsoftNames()) {
            const Type *chars = types_.pointerTo(types_.get(Kind::Char));
            const Type *offType = ptrdiffType();
            ExprPtr asChars(new Cast(chars, std::move(e)));
            asChars->setType(chars);
            int slot = const_cast<Parser *>(this)->allocateFrameSlot(chars);
            std::string temp = ".vp" +
                std::to_string(const_cast<Parser *>(this)->refTemps_++);
            ExprPtr held(Var::local(temp, slot));
            held->setType(chars);
            ExprPtr save(new Assign(std::move(held), std::move(asChars)));
            save->setType(chars);

            ExprPtr obj(Var::local(temp, slot));
            obj->setType(chars);
            ExprPtr asTable(new Cast(types_.pointerTo(chars), std::move(obj)));
            asTable->setType(types_.pointerTo(chars));
            ExprPtr vptr(new Unary('*', std::move(asTable)));
            vptr->setType(chars);
            ExprPtr backNum(new Num(vbBack));
            backNum->setType(offType);
            ExprPtr at(new Binary(BinOp::Add, std::move(vptr), std::move(backNum)));
            at->setType(chars);
            ExprPtr asOff(new Cast(types_.pointerTo(offType), std::move(at)));
            asOff->setType(types_.pointerTo(offType));
            ExprPtr delta(new Unary('*', std::move(asOff)));
            delta->setType(offType);

            ExprPtr from(Var::local(temp, slot));
            from->setType(chars);
            ExprPtr moved(new Binary(BinOp::Add, std::move(from), std::move(delta)));
            moved->setType(chars);
            ExprPtr asTo(new Cast(to, std::move(moved)));
            asTo->setType(to);
            ExprPtr whole(new Comma(std::move(save), std::move(asTo)));
            whole->setType(to);
            return whole;
        }
        if (off > 0) {
            const Type *chars = types_.pointerTo(types_.get(Kind::Char));
            ExprPtr asChars(new Cast(chars, std::move(e)));
            asChars->setType(chars);

            int slot = const_cast<Parser *>(this)->allocateFrameSlot(chars);
            std::string temp = ".bp" + std::to_string(const_cast<Parser *>(this)->refTemps_++);
            ExprPtr held(Var::local(temp, slot));
            held->setType(chars);
            ExprPtr save(new Assign(std::move(held), std::move(asChars)));
            save->setType(chars);

            ExprPtr test(Var::local(temp, slot));
            test->setType(chars);
            ExprPtr shift(Var::local(temp, slot));
            shift->setType(chars);
            ExprPtr by(new Num(static_cast<long long>(off)));
            by->setType(types_.intType());
            ExprPtr moved(new Binary(BinOp::Add, std::move(shift), std::move(by)));
            moved->setType(chars);
            ExprPtr zero(new Num(0LL));
            zero->setType(chars);
            ExprPtr pick(new Conditional(std::move(test), std::move(moved),
                                         std::move(zero)));
            pick->setType(chars);

            ExprPtr both(new Comma(std::move(save), std::move(pick)));
            both->setType(chars);
            ExprPtr out(new Cast(to, std::move(both)));
            out->setType(to);
            return out;
        }
    }

    // [conv.bool]: a member pointer's null is not the number 0 (a data member pointer's is -1), which the
    // comparison knows.
    if (to->isBool() && (e->type()->isMemberPointer() || e->type()->isMemberFunctionPointer())) {
        ExprPtr none(new Num(static_cast<long long>(0)));
        none->setType(types_.intType());
        ExprPtr test = const_cast<Parser *>(this)->comparison(BinOp::Ne, std::move(e), std::move(none), peek().pos);
        test->setType(to);
        return test;
    }

    // A conversion to bool is not a narrowing: [conv.bool] makes every non-zero
    // value true, so (bool)256 is true where (char)256 is 0. Lowered here to a
    // comparison against zero rather than taught to three backends as a cast.
    if (to->isBool() && !e->type()->isBool() && e->type()->isScalar()) {
        const Type *from = e->type();
        ExprPtr zero;
        if (from->isFloating()) {
            zero.reset(new Num(static_cast<long double>(0)));
            zero->setType(from);
        } else {
            ExprPtr n(new Num(static_cast<long long>(0)));
            n->setType(types_.intType());
            zero = convert(std::move(n), from, true);
        }
        ExprPtr test(new Binary(BinOp::Ne, std::move(e), std::move(zero)));
        test->setType(to);
        return test;
    }

    return ExprPtr(new Cast(to, std::move(e)));
}

// **[expr.cast]/4: no cast converts between two unrelated classes on its own.**
// A conversion function is the one road and `convert` takes it; without one
// the bytes were reinterpreted in silence, which clang refuses.
void Parser::refuseUnrelatedClassCast(const Expr &v, const Type *to,
                                      std::size_t pos, const char *what) {
    const Type *from = v.type();
    // **[expr.static.cast]/4 and [expr.cast]/4: a class reaches a scalar by its conversion function alone** -
    // `static_cast<long>(q)` for a q with none is ill-formed, and the Cast would read its bytes.
    if (from != nullptr && from->unqualified()->isStructOrUnion() && !to->isReference() && !to->isVoid() &&
        !to->unqualified()->isStructOrUnion()) {
        if (conversionFunction(from, to->unqualified(), true) != nullptr) return;
        src_.fail(pos, std::string(what) + " from '" + from->describe() + "' to '" + to->describe() + "' - '" +
                       from->unqualified()->describe() + "' has no conversion function to '" +
                       to->unqualified()->describe() + "' or to a type that converts to it");
    }
    if (from == nullptr || !to->unqualified()->isStructOrUnion()) return;
    // **A scalar cast to a class has one road, a converting constructor** - without one the Cast
    // that follows would reinterpret the bytes, which `(W)2.5` once did.
    if (!from->unqualified()->isStructOrUnion()) {
        if (convertingConstructor(to, v, pos, true) != nullptr) return;
        src_.fail(pos, std::string(what) + " from '" + from->describe() + "' to '" +
                       to->describe() + "' - no constructor of '" + to->unqualified()->describe() +
                       "' takes one, or two take one equally well");
    }
    if (from->unqualified() == to->unqualified()) return;
    if (publicBaseOffset(from, to) > -1 || publicBaseOffset(to, from) > -1) return;
    if (conversionFunction(from, to->unqualified(), true) != nullptr) return;
    if (convertingConstructor(to, v, pos, true) != nullptr) return;
    src_.fail(pos, std::string(what) + " from '" + from->describe() + "' to '" +
                   to->describe() + "' - the two classes are unrelated and '" +
                   from->unqualified()->describe() + "' has no conversion "
                   "function to '" + to->unqualified()->describe() + "', so "
                   "there is nothing for a cast to do but reinterpret the "
                   "bytes, which no cast does");
}

ExprPtr Parser::decay(ExprPtr e) {
    if (!e->type()->isArray()) return e;
    const Type *to = types_.pointerTo(e->type()->pointee());
    return ExprPtr(new Cast(to, std::move(e)));
}

void Parser::requireScalar(const Expr &e, std::size_t pos, const char *what) {
    // [dcl.enum]/10: a scoped enumeration is no truth value - but it may govern a switch, [stmt.switch]/2.
    if (e.type()->unqualified()->isScopedEnumeration() && std::string(what) == "unary '+'")
        refuseScopedOperand(e.type(), nullptr, "+", pos);
    if (e.type()->unqualified()->isScopedEnumeration() && !atSwitchCondition())
        refuseScopedConversion(e.type(), types_.get(Kind::Bool), pos);
    if (!e.type()->isScalar())
        src_.fail(pos, std::string(what) + " needs a number or a pointer, not '" +
                       e.type()->describe() + "'");
}

// At the `)` of `switch (...)`: the token before the matching `(` says which statement this is.
bool Parser::atSwitchCondition() const {
    if (!peek().is(")") || at_ == 0) return false;
    int depth = 0;
    for (std::size_t i = at_; i-- > 0; ) {
        if (tokens_[i].is(")")) depth++;
        else if (tokens_[i].is("(") && depth-- == 0) return i > 0 && tokens_[i - 1].is("switch");
    }
    return false;
}

// A string literal reaches here wrapped in the Cast that decayed it from an
// array, so the literal has to be looked for underneath.
static bool isStringLiteral(const Expr &e) {
    if (dynamic_cast<const StrLit *>(&e) != nullptr) return true;
    if (const Cast *c = dynamic_cast<const Cast *>(&e))
        return isStringLiteral(c->value());
    return false;
}

// [conv.qual]: a pointer may gain const and never lose it, and const below the
// first level counts only where every level above it is const too. Then whether
// `base` is a public base of `derived`, and at what offset - every base, not one.
int publicBaseOffset(const Type *derived, const Type *base) {
    if (derived == nullptr || base == nullptr) return -1;
    const Type *d = derived->unqualified();
    const Type *b = base->unqualified();
    if (d == b) return 0;
    const std::vector<Type::BaseSpec> &bases = d->bases();
    for (std::size_t i = 0; i < bases.size(); i++) {
        if (bases[i].access != Access::Public) continue;
        int deeper = publicBaseOffset(bases[i].type, b);
        if (deeper >= 0) return bases[i].offset + deeper;
    }
    return -1;
}

// **How many `base` subobjects a `derived` holds**, whatever their access (a
// cast ignores it, [expr.cast]/4) - [class.mi]/4: one per non-virtual path,
// one for all virtual ones together; a virtual base reached twice counts once.
static void countBaseSubobjects(const Type *d, const Type *base, int *plain, bool *shared,
                                std::set<const Type *> *walkedVirtual) {
    for (const Type::BaseSpec &s : d->unqualified()->bases()) {
        if (!s.direct) continue;
        const Type *bt = s.type->unqualified();
        if (s.isVirtual && !walkedVirtual->insert(bt).second) continue;
        if (bt == base) {
            if (s.isVirtual) *shared = true;
            else (*plain)++;
            continue;
        }
        countBaseSubobjects(bt, base, plain, shared, walkedVirtual);
    }
}

static int baseSubobjects(const Type *derived, const Type *base) {
    int plain = 0;
    bool shared = false;
    std::set<const Type *> walked;
    countBaseSubobjects(derived, base->unqualified(), &plain, &shared, &walked);
    return plain + (shared ? 1 : 0);
}

static int anyBaseOffset(const Type *derived, const Type *base) {
    if (derived == nullptr || base == nullptr) return -1;
    const Type *d = derived->unqualified();
    if (d == base->unqualified()) return 0;
    for (const Type::BaseSpec &b : d->bases()) {
        const int deeper = anyBaseOffset(b.type, base);
        if (deeper >= 0) return b.offset + deeper;
    }
    return -1;
}

static bool publiclyDerivedFrom(const Type *derived, const Type *base) {
    if (derived == nullptr || base == nullptr) return false;
    if (derived->unqualified() == base->unqualified()) return false;
    return publicBaseOffset(derived, base) >= 0;
}

static bool throughVirtualBase(const Type *derived, const Type *base) {
    const Type *d = derived->unqualified();
    const std::vector<Type::BaseSpec> &bases = d->bases();
    for (std::size_t i = 0; i < bases.size(); i++) {
        if (bases[i].type->unqualified() == base->unqualified()) return bases[i].isVirtual;
        if (publicBaseOffset(bases[i].type, base) >= 0)
            return bases[i].isVirtual || throughVirtualBase(bases[i].type, base);
    }
    return false;
}

// **[conv.mem]/2: `int B::*` converts to `int D::*` for a D derived from B**,
// the offset or the `this` adjustment growing by the base's. Through a
// virtual base it is ill-formed, which both oracles say; -1 here does too.
int Parser::memberPointerToDerived(const Type *from, const Type *to) const {
    const Type *f = from->unqualified(), *t = to->unqualified();
    const bool data = f->isMemberPointer() && t->isMemberPointer();
    const bool fn = f->isMemberFunctionPointer() && t->isMemberFunctionPointer();
    if (!data && !fn) return -1;
    if (f->enclosing() == nullptr || t->enclosing() == nullptr) return -1;
    if (data ? f->pointee()->unqualified() != t->pointee()->unqualified()
             : f->pointee() != t->pointee()) return -1;
    if (f->enclosing()->unqualified() == t->enclosing()->unqualified()) return 0;
    const int off = publicBaseOffset(t->enclosing(), f->enclosing());
    if (off < 0) return -1;
    if (throughVirtualBase(t->enclosing(), f->enclosing())) return -1;
    return off;
}

ExprPtr Parser::convertMemberPointer(ExprPtr e, const Type *to, int shift) const {
    Parser *self = const_cast<Parser *>(this);
    const Type *ft = e->type()->unqualified();
    if (ft->isMemberPointer()) {
        // Itanium's ptrdiff_t and Microsoft's int, held in the widest lane;
        // a null pointer stays the null of its destination.
        const Type *wide = types_.get(Kind::LongLong);
        const bool fourBytes = ft->size(target_) == 4;
        const Type *lane = fourBytes ? types_.get(Kind::Int) : wide;
        const int slot = self->allocateFrameSlot(ft);
        const std::string name = ".mq" + std::to_string(self->refTemps_++);
        auto held = [&]() { ExprPtr v(Var::local(name, slot)); v->setType(ft); return v; };
        auto asLane = [&]() { ExprPtr c(new Cast(lane, held())); c->setType(lane); return c; };
        auto num = [&](long long n, const Type *t) { ExprPtr v(new Num(n)); v->setType(t); return v; };
        ExprPtr keep(new Assign(held(), std::move(e)));
        keep->setType(ft);
        ExprPtr isNull(new Binary(BinOp::Eq, asLane(), num(-1, lane)));
        isNull->setType(types_.get(Kind::Bool));
        const bool virtualModel = target_.microsoftNames() && to->size(target_) == 8;
        ExprPtr nullTo = num(virtualModel ? -4294967296LL : -1LL, wide);
        ExprPtr widened(new Cast(wide, asLane()));
        widened->setType(wide);
        ExprPtr moved(new Binary(BinOp::Add, std::move(widened), num(shift, wide)));
        moved->setType(wide);
        ExprPtr chosen(new Conditional(std::move(isNull), std::move(nullTo), std::move(moved)));
        chosen->setType(wide);
        ExprPtr asTo(new Cast(to, std::move(chosen)));
        asTo->setType(to);
        ExprPtr whole(new Comma(std::move(keep), std::move(asTo)));
        whole->setType(to);
        return whole;
    }
    // The pair, rebuilt in the destination's shape: the code word copied, the
    // adjustment grown by the base's offset unless the pointer is null, and
    // Microsoft's vbtable index carried or zero.
    const int fromSlot = self->allocateFrameSlot(ft);
    const int toSlot = self->allocateFrameSlot(to);
    const std::string fromName = ".mq" + std::to_string(self->refTemps_++);
    const std::string toName = ".mq" + std::to_string(self->refTemps_++);
    auto src = [&](const char *m) {
        const Member *mm = ft->findMember(m);
        if (mm == nullptr) return ExprPtr();
        ExprPtr v(Var::local(fromName, fromSlot)); v->setType(ft);
        ExprPtr a(new MemberAccess(std::move(v), m, mm->offset, 0, 0)); a->setType(mm->type); return a;
    };
    auto dst = [&](const char *m) {
        const Member *mm = to->findMember(m);
        ExprPtr v(Var::local(toName, toSlot)); v->setType(to);
        ExprPtr a(new MemberAccess(std::move(v), m, mm->offset, 0, 0)); a->setType(mm->type); return a;
    };
    auto num = [&](long long n, const Type *t) { ExprPtr v(new Num(n)); v->setType(t); return v; };
    ExprPtr chain(new Assign(ExprPtr(Var::local(fromName, fromSlot)), std::move(e)));
    chain->setType(ft);
    static const char *const fields[] = { "$fn", "$adj", "$vbi" };
    for (std::size_t i = 0; i < 3; i++) {
        const Member *tm = to->findMember(fields[i]);
        if (tm == nullptr) continue;
        ExprPtr value = src(fields[i]);
        if (value == nullptr) value = num(0, tm->type);
        else if (i == 0 && value->type() != tm->type) value->setType(tm->type);
        if (i == 1 && shift != 0) {
            ExprPtr code = src("$fn");
            ExprPtr zero(new Cast(code->type(), num(0, types_.get(Kind::LongLong))));
            zero->setType(code->type());
            ExprPtr live(new Binary(BinOp::Ne, std::move(code), std::move(zero)));
            live->setType(types_.get(Kind::Bool));
            ExprPtr grow(new Conditional(std::move(live), num(shift, tm->type), num(0, tm->type)));
            grow->setType(tm->type);
            ExprPtr sum(new Binary(BinOp::Add, std::move(value), std::move(grow)));
            sum->setType(tm->type);
            value = std::move(sum);
        }
        ExprPtr put(new Assign(dst(fields[i]), std::move(value)));
        put->setType(tm->type);
        ExprPtr both(new Comma(std::move(chain), std::move(put)));
        both->setType(tm->type);
        chain = std::move(both);
    }
    ExprPtr whole(Var::local(toName, toSlot));
    whole->setType(to);
    ExprPtr at(new Unary('&', std::move(whole)));
    at->setType(types_.pointerTo(to));
    ExprPtr seq(new Comma(std::move(chain), std::move(at)));
    seq->setType(types_.pointerTo(to));
    ExprPtr made(new Unary('*', std::move(seq)));
    made->setType(to);
    return made;
}

// **An array's cv-qualification is its element's** ([conv.qual]/1), so
// `char (*)[3]` converts to `const char (*)[3]` as `char **` does to
// `const char *const *` (review A19); the step through an array keeps the extent.
static const Type *cvOwner(const Type *t) {
    while (t->isArray()) t = t->pointee();
    return t;
}

// [class.copy]/8 and /11: the implicit copy and move constructors take `const X &` and `X &&`, and neither
// binds a volatile object - so a volatile class object is copied only by a constructor written for it.
void Parser::refuseVolatileClassCopy(const Type *from, const Type *to, const std::string &what,
                                     std::size_t pos) const {
    if (from == nullptr || !from->isVolatile() || from->isMemberFunctionPointer()) return;
    const Type *cls = to->unqualified();
    if (!from->unqualified()->isStructOrUnion() || !cls->isStructOrUnion() || cls->isMemberFunctionPointer()) return;
    if (const_cast<Parser *>(this)->copyConstructorOf(cls) != nullptr ||
        const_cast<Parser *>(this)->moveConstructorOf(cls) != nullptr) return;
    src_.fail(pos, what + " is '" + to->describe() + "' and this is '" + from->describe() + "' - the copy "
                   "constructor the compiler writes takes a 'const " + cls->describe() + " &', which does not "
                   "bind a volatile object; a constructor taking 'const volatile " + cls->describe() +
                   " &' would");
}
static bool qualificationConvertible(const Type *from, const Type *to) {
    bool prefixConst = true;
    for (;;) {
        if (from->unqualified() == to->unqualified()) return true;
        if (from->isArray() && to->isArray()) {
            if (from->length() != to->length()) return false;
            from = from->pointee();
            to = to->pointee();
            continue;
        }
        if (!from->isPointer() || !to->isPointer()) return false;
        from = from->pointee();
        to = to->pointee();
        // Neither qualifier may be lost, and a level that gains either has const at every level above it.
        const Type *f = cvOwner(from), *t = cvOwner(to);
        const bool fromConst = f->isConst(), toConst = t->isConst();
        const bool fromVolatile = f->isVolatile(), toVolatile = t->isVolatile();
        if ((fromConst && !toConst) || (fromVolatile && !toVolatile)) return false;
        if (((!fromConst && toConst) || (!fromVolatile && toVolatile)) && !prefixConst) return false;
        prefixConst = prefixConst && toConst;
    }
}

// ------------------------------------------------------------------ overloading
// [over.match] reduced to what rung 2 needs: a conversion sequence is ranked, the
// best viable function is the one no other beats, the unrankable is not viable.

const Type *Parser::decayedType(const Type *t) {
    if (t->isArray()) return types_.pointerTo(t->pointee());
    if (t->isFunction()) return types_.pointerTo(t);
    return t;
}

// The integral and floating promotions, [conv.prom] and [conv.fpprom], and only those - every other arithmetic pairing is a conversion, which ranks below; it is what makes f(int) beat f(double) for a char argument.
static bool isPromotion(const Type *from, const Type *to) {
    if (from->isScopedEnumeration()) return false;
    // [conv.prom]/3: an enumeration to the type its underlying integer promotes to.
    if (from->isEnumeration() && !to->isEnumeration()) {
        const Kind k = from->kind();
        const bool small = k == Kind::Bool || k == Kind::Char || k == Kind::SChar ||
                           k == Kind::UChar || k == Kind::Short || k == Kind::UShort;
        // [conv.prom]/4: one whose underlying type is fixed promotes to that type too - `enum E : short` to
        // short (1.80: that was a conversion, so `f(short)` and `f(long)` were ambiguous for it).
        if (from->hasFixedUnderlying() && to->kind() == k) return true;
        return to->kind() == (small ? Kind::Int : k);
    }
    if (to->kind() == Kind::Int) {
        switch (from->kind()) {
            case Kind::Bool: case Kind::Char: case Kind::SChar: case Kind::UChar:
            case Kind::Short: case Kind::UShort: case Kind::WChar: case Kind::Char16:
                return true;
            default:
                return false;
        }
    }
    if (to->kind() == Kind::UInt && from->kind() == Kind::Char32) return true;
    return to->kind() == Kind::Double && from->kind() == Kind::Float;
}
// The function an expression designates as an lvalue: one of function type (a reference to a
// function, read), or a function's name, which arrives as `&f`. Null for anything else.
const Type *Parser::functionDesignated(const Expr &e) const {
    if (e.type()->isFunction()) return e.type();
    if (const Unary *u = dynamic_cast<const Unary *>(&e))
        if (u->op() == '&' && u->operand().type()->isFunction()) return u->operand().type();
    return nullptr;
}

Parser::Rank Parser::rankArgument(const Expr &arg, const Type *param) {
    const Type *given = arg.type();
    // [temp.inst]/1: a conversion to or from a class needs the class defined. A member class of a class
    // template's specialization is defined when first reached, and until 1.80 being a parameter's type did not
    // reach it: `int f(Where at);` in `Str<char>` took no `const char *` for its `Where(const char *)`.
    {
        const Type *to = param->isReference() ? param->pointee() : param;
        if (to->unqualified()->isStructOrUnion() && !to->unqualified()->isComplete()) realizeMemberClass(to);
        if (given != nullptr) {
            const Type *from = given->isReference() ? given->pointee() : given;
            if (from->unqualified()->isStructOrUnion() && !from->unqualified()->isComplete()) realizeMemberClass(from);
        }
    }

    // A reference parameter binds or it does not; there is no conversion to
    // rank.
    if (param->isReference()) {
        const Type *want = param->pointee();
        // The reference may add const and volatile and never take either away; and [dcl.init.ref]/5 binds
        // a temporary to an lvalue reference to non-volatile const - `const volatile int &` takes no 3.
        auto dropsCv = [](const Type *w, const Type *g) {
            return (!w->isConst() && g->isConst()) || (!w->isVolatile() && g->isVolatile());
        };
        auto addsCv = [](const Type *w, const Type *g) {
            return (w->isConst() && !g->isConst()) || (w->isVolatile() && !g->isVolatile());
        };
        // An rvalue reference takes one too: `f(std::string &&)` is viable for `f("lit")`.
        const bool takesTemporary = param->isRValueReference() || (want->isConst() && !want->isVolatile());

        // **A reference to a function binds a function lvalue of its type** - [dcl.init.ref]/5,
        // an exact match; a name arrives as its address, `&f`, and that is the same function.
        if (want->isFunction()) {
            const Type *fn = functionDesignated(arg);
            return fn != nullptr && fn->unqualified() == want->unqualified() ? Rank::Identity
                                                                             : Rank::None;
        }

        // **A reference to an array binds to an array of the same length**,
        // without the decay a by-value parameter would apply.
        if (want->isArray() && given->isArray() && want->sameArrayShape(given)) {
            const Type *we = want->innermostElement();
            const Type *ge = given->innermostElement();
            if (we->unqualified() == ge->unqualified()) {
                if (dropsCv(we, ge)) return Rank::None;
                // Which reference takes it is the argument's category, as for any other type below:
                // `forward<const char (&)[4]>` has a `T &` and a `T &&` overload, and an lvalue takes the first.
                if (param->isRValueReference() && isLvalue(arg)) return Rank::None;
                return addsCv(we, ge) ? Rank::Qualification : Rank::Identity;
            }
        }

        // **A reference to a base binds to a derived object** - [dcl.init.ref], a derived-to-base
        // Conversion, so `f(Base &)` loses to `f(Derived &)` for a Derived; and /5 binds a const
        // one to a derived class temporary too, `f(D(3))` for `f(const B &)`.
        if (want->unqualified() != given->unqualified() &&
            accessibleBaseOffset(given, want) > -1 && !param->isRValueReference() &&
            (isLvalue(arg) || (takesTemporary && given->unqualified()->isStructOrUnion()))) {
            if (dropsCv(want, given)) return Rank::None;
            return Rank::Conversion;
        }
        // `B &&` is reference-related to a derived D, so [dcl.init.ref]/5 refuses a D lvalue outright - no copy
        // constructor of B makes a temporary for it - and binds a D xvalue or prvalue to its base.
        if (want->unqualified() != given->unqualified() && param->isRValueReference() &&
            accessibleBaseOffset(given, want) > -1) {
            if (isLvalue(arg) || dropsCv(want, given)) return Rank::None;
            return Rank::Conversion;
        }

        if (want->unqualified() != given->unqualified()) {
            // **A `const T &` takes a temporary, so it takes a conversion.**
            // [over.ics.user] with [dcl.init.ref]: the converting constructor
            // makes an object and the reference binds to it.
            if (takesTemporary && rankingConversion_ == 0) {
                rankingConversion_++;
                // A class target is reached by its converting constructor or by
                // the argument's conversion function, [over.ics.user]; the second
                // was missing, and `take(const B &)` refused an A with `operator B`.
                const bool usable =
                    want->unqualified()->isStructOrUnion()
                        ? (convertingConstructor(want, arg) != nullptr ||
                           (given->unqualified()->isStructOrUnion() &&
                            conversionFunction(given, want->unqualified()) != nullptr))
                        : (given->unqualified()->isStructOrUnion() &&
                           conversionFunction(given, want) != nullptr);
                rankingConversion_--;
                if (usable) return userDefinedRank(given, want);
            }
            // [dcl.init.ref]/5, [over.ics.ref]/1: an lvalue reference binds what a class's conversion function to an
            // lvalue of the type returns - `bump(std::ref(n))` for `bump(int &)` - a user-defined conversion (1.75).
            if (!param->isRValueReference() && referenceConversion(given, want) != nullptr) return Rank::UserDefined;
            // **And a standard conversion makes that temporary too.**
            if (takesTemporary) {
                const Rank direct = rankArgument(arg, want->unqualified());
                if (direct != Rank::None) return direct;
            }
            return Rank::None;
        }
        if (dropsCv(want, given)) return Rank::None;

        // **Which reference will take this argument is a question about the
        // argument, not about a conversion.** An rvalue reference is not viable
        // for an object that has an address, and the better match where both are.
        if (param->isRValueReference() && isLvalue(arg)) return Rank::None;
        // `const T &&` for a `T` rvalue adds a qualifier, as `const T &` does below - [over.ics.rank]/3.2.6,
        // so that `fwd(std::move(x))` picks fwd<int> over the fwd<const int> an earlier call made (1.27).
        if (param->isRValueReference())
            return addsCv(want, given) ? Rank::Qualification : Rank::Identity;

        // **A value has no lvalue to bind, so `T &` is not viable for one**.
        if (!isLvalue(arg)) return takesTemporary ? Rank::Qualification : Rank::None;

        return addsCv(want, given) ? Rank::Qualification : Rank::Identity;
    }

    const Type *from = decayedType(given);
    const Type *to = param;

    if (from == to) return Rank::Identity;
    // Top-level const on the parameter is not part of its type for this purpose.
    if (from->unqualified() == to->unqualified()) return Rank::Identity;

    // Nothing converts to an enumeration implicitly - [conv.integral]/1 starts from one - and a
    // scoped one converts to nothing, [dcl.enum]/10.
    if (from->isArithmetic() && to->unqualified()->isEnumeration()) return Rank::None;
    if (from->isScopedEnumeration()) return Rank::None;
    if (from->isArithmetic() && to->isArithmetic())
        return isPromotion(from, to) ? Rank::Promotion : Rank::Conversion;

    if (to->isPointer() && from->isPointer()) {
        // A qualification conversion - char * to const char *.
        if (qualificationConvertible(from, to)) return Rank::Qualification;
        // Derived * to Base * is a pointer conversion, which ranks below a
        // promotion - so f(Base *) loses to f(Derived *) for a Derived *,
        // which is what [over.ics.rank] asks for.
        if (from->pointee()->unqualified() != to->pointee()->unqualified() &&
            accessibleBaseOffset(from->pointee(), to->pointee()) >= 0 &&
            (to->pointee()->isConst() || !from->pointee()->isConst()))
            return Rank::Conversion;
        if (to->pointee()->isVoid() && !to->pointee()->isConst() &&
            from->pointee()->isConst())
            return Rank::None;
        // [conv.ptr]/2: any object pointer to void *, and never back without a cast.
        if (to->pointee()->isVoid()) return Rank::Conversion;
        return Rank::None;
    }

    if (to->isPointer() && from->isInteger())
        return isNullConstant(arg) ? Rank::Conversion : Rank::None;

    // **`nullptr` converts to any pointer and to any pointer to member**, both
    // pointer conversions - so `f(void *)` and `f(char *)` are ambiguous for it, as
    // clang reports. **And not to bool here**: that conversion is direct-init only.
    if (from->isNullPtr() && (to->isPointer() || to->isMemberPointer() || to->isMemberFunctionPointer()))
        return Rank::Conversion;
    // [conv.mem]/1: the constant 0 is a null pointer to member too, a member function's included.
    if ((to->isMemberPointer() || to->isMemberFunctionPointer()) && from->isInteger())
        return isNullConstant(arg) ? Rank::Conversion : Rank::None;
    if (memberPointerToDerived(from, to) >= 0 &&
        from->unqualified()->enclosing()->unqualified() != to->unqualified()->enclosing()->unqualified())
        return Rank::Conversion;
    // [conv.bool]: a boolean conversion, and [over.ics.rank]/4 puts it behind every other of its rank.
    if (to->unqualified()->isBool() && (from->isPointer() || from->isMemberPointer() || from->isMemberFunctionPointer()))
        return Rank::BooleanConversion;

    // And the literal 0 converts *to* std::nullptr_t, at the same rank - which
    // is why `f(0)` against `f(decltype(nullptr))` and `f(char *)` is
    // ambiguous, measured.
    if (to->isNullPtr() && from->isInteger())
        return isNullConstant(arg) ? Rank::Conversion : Rank::None;

    // [over.best.ics]/6: a derived object passed by value to a base is a derived-to-base Conversion.
    if (to->unqualified()->isStructOrUnion() && from->unqualified()->isStructOrUnion() &&
        to->unqualified() != from->unqualified() && accessibleBaseOffset(from, to->unqualified()) >= 0)
        return Rank::Conversion;

    // **Last of all, a user-defined conversion** - [over.ics.rank]/2 puts one
    // below every standard conversion, so this is asked only where nothing
    // above it answered.
    if (rankingConversion_ == 0) {
        rankingConversion_++;
        const bool usable =
            to->unqualified()->isStructOrUnion()
                ? (convertingConstructor(to, arg) != nullptr ||
                   (from->unqualified()->isStructOrUnion() &&
                    conversionFunction(from, to->unqualified()) != nullptr))
                : (from->unqualified()->isStructOrUnion() &&
                   conversionFunction(from, to) != nullptr);
        rankingConversion_--;
        if (usable) return userDefinedRank(from, to);
    }

    return Rank::None;
}

// [over.ics.rank]/3.3: sequences through one conversion function differ by the
// conversion after it - the target's distance from its result is the sub-rank
// (`lng(long)` beats `lng(int)`); a class target keeps the plain rank.
Parser::Rank Parser::userDefinedRank(const Type *from, const Type *to) {
    const Type *want = to->unqualified();
    if (want->isStructOrUnion()) return Rank::UserDefined;
    const Signature *via = soleNumericConversion(from);
    if (via == nullptr) return Rank::UserDefined;
    const Type *gives = via->returns->unqualified();
    if (gives == want) return Rank::UserDefined;
    // [over.ics.rank]/3: through the one conversion function, the second standard conversion tells two
    // parameters apart - `f(int *)` before `f(const int *)` for a class that gives an `int *`. A pointer never
    // promotes, so the rank between is free for the qualification.
    if (gives->isPointer() && want->isPointer() && qualificationConvertible(gives, want))
        return Rank::UserDefinedPromotion;
    if (gives->isArithmetic() && want->isArithmetic() && isPromotion(gives, want))
        return Rank::UserDefinedPromotion;
    return Rank::UserDefinedConversion;
}

std::string Parser::describeSignature(const Signature &f) {
    std::string out = f.name + "(";
    for (std::size_t i = 0; i < f.params.size(); i++) {
        if (i > 0) out += ", ";
        out += f.params[i]->describe();
    }
    if (f.variadic) out += f.params.empty() ? "..." : ", ...";
    out += ")";
    if (f.constThis) out += " const";
    if (f.volatileThis) out += " volatile";
    return out + (f.refThis == RefQual::LValue ? " &" : f.refThis == RefQual::RValue ? " &&" : "");
}

// [over.match.funcs]/4: `X &&` for `&&`, else `X &` - and an rvalue still binds an unqualified
// member's; `const &` binds an rvalue as any const lvalue reference does, [dcl.init.ref]/5.
bool Parser::refQualifierViable(RefQual ref, bool constThis, bool objectLvalue) {
    if (ref == RefQual::None) return true;
    if (ref == RefQual::RValue) return !objectLvalue;
    return objectLvalue || constThis;
}

std::string Parser::refQualifierRefusal(const std::string &name, bool objectLvalue) {
    return "'this' argument to member function '" + name + "' is an " +
           (objectLvalue ? "lvalue, but function has rvalue ref-qualifier"
                         : "rvalue, but function has non-const lvalue ref-qualifier");
}

// The best viable function, or a refusal naming every candidate: F beats G
// when it is no worse on every argument and better on one, and two that each
// win an argument are the ambiguity.
const Type *Parser::rankedParameter(const Signature &f, std::size_t i,
                                    bool ranksObject) {
    if (ranksObject) {
        if (i == 0) return nullptr;
        i--;
    }
    return i < f.params.size() ? f.params[i] : nullptr;
}

// **A by-value parameter and a reference to the same type are the same
// match**, and [over.ics.rank] gives neither a way to beat the other: one
// copies the argument, one binds it, and both are the identity conversion.
bool Parser::sameMatchEitherWay(const Type *pa, const Type *pb) {
    if (pa == nullptr || pb == nullptr) return false;
    const bool ra = pa->isReference(), rb = pb->isReference();
    if (ra == rb) return false;
    const Type *va = ra ? pa->pointee()->unqualified() : pa->unqualified();
    const Type *vb = rb ? pb->pointee()->unqualified() : pb->unqualified();
    return va == vb;
}

// **Two parameters that took one argument at one rank**, [over.ics.rank]: the
// rvalue ref for an rvalue (/3.2.3), less cv (/3.2.6, /4.3), the nearer base or
// a class over void * (/4.4, needs the argument); user-defined ranks are not.
int Parser::closerTarget(const Type *pa, const Type *pb, const Type *given) const {
    if (pa == nullptr || pb == nullptr || given == nullptr) return 0;
    // [over.ics.rank]/4 as CWG 1601 has it: an enumeration whose underlying type is fixed promotes better to
    // that type than to what the type promotes to - `f(short)` before `f(int)` for `enum E : short`.
    if (given->unqualified()->hasFixedUnderlying() && !pa->isReference() && !pb->isReference() &&
        pa->unqualified()->isArithmetic() && pb->unqualified()->isArithmetic() &&
        !pa->unqualified()->isEnumeration() && !pb->unqualified()->isEnumeration()) {
        const Kind k = given->unqualified()->kind();
        const bool a = pa->unqualified()->kind() == k, b = pb->unqualified()->kind() == k;
        if (a != b) return a ? 1 : -1;
    }
    struct Nearer {
        static int derived(const Parser &p, const Type *a, const Type *b, const Type *from) {
            if (from == nullptr || !from->isStructOrUnion()) return 0;
            if (a == b || !a->isStructOrUnion() || !b->isStructOrUnion()) return 0;
            if (p.accessibleBaseOffset(a, b) >= 0) return 1;
            if (p.accessibleBaseOffset(b, a) >= 0) return -1;
            return 0;
        }
    };
    // [over.ics.rank]/3.2.5-6: of two targets that differ only in cv-qualification, the one whose qualifiers are a
    // proper subset of the other's is the better - `const T *` over `const volatile T *`.
    struct Cv {
        static int closer(const Type *a, const Type *b) {
            const bool aInB = (!a->isConst() || b->isConst()) && (!a->isVolatile() || b->isVolatile());
            const bool bInA = (!b->isConst() || a->isConst()) && (!b->isVolatile() || a->isVolatile());
            return aInB && !bInA ? 1 : bInA && !aInB ? -1 : 0;
        }
    };
    if (pa->isReference() && pb->isReference()) {
        const Type *ta = pa->pointee(), *tb = pb->pointee();
        if (ta->unqualified() == tb->unqualified()) {
            if (const int cv = Cv::closer(ta, tb)) return cv;
            if (pa->isRValueReference() != pb->isRValueReference())
                return pa->isRValueReference() ? 1 : -1;
            return 0;
        }
        return Nearer::derived(*this, ta->unqualified(), tb->unqualified(), given->unqualified());
    }
    if (pa->isPointer() && pb->isPointer() && (given->isPointer() || given->isArray())) {
        const Type *ta = pa->pointee(), *tb = pb->pointee();
        const Type *object = given->pointee()->unqualified();   // an array decays to its element's
        if (ta->unqualified() == tb->unqualified())
            return Cv::closer(ta, tb);
        if (tb->isVoid() && ta->unqualified()->isStructOrUnion() && object->isStructOrUnion()) return 1;
        if (ta->isVoid() && tb->unqualified()->isStructOrUnion() && object->isStructOrUnion()) return -1;
        return Nearer::derived(*this, ta->unqualified(), tb->unqualified(), object);
    }
    if (!pa->isReference() && !pb->isReference())
        return Nearer::derived(*this, pa->unqualified(), pb->unqualified(), given->unqualified());
    return 0;
}

bool Parser::betterCandidate(const std::vector<Rank> &a,
                             const std::vector<Rank> &b,
                             const Signature &fa, const Signature &fb,
                             bool ranksObjectA, bool ranksObjectB) const {
    bool better = false, worse = false;
    for (std::size_t i = 0; i < a.size() && i < b.size(); i++) {
        const Type *pa = rankedParameter(fa, i, ranksObjectA);
        const Type *pb = rankedParameter(fb, i, ranksObjectB);
        // [over.ics.rank]/3.2.6: two members binding the object with added qualifiers - the one adding fewer
        // is better: `load() const` over `load() const volatile` for a plain object.
        const bool objectInBase = a[0] == Rank::Conversion && b[0] == Rank::Conversion && fa.owner == fb.owner &&
                                  (fa.constThis != fb.constThis || fa.volatileThis != fb.volatileThis);
        if (i == 0 && ranksObjectA && ranksObjectB &&
            ((a[0] == Rank::Qualification && b[0] == Rank::Qualification) || objectInBase)) {
            const bool aWithinB = (!fa.constThis || fb.constThis) && (!fa.volatileThis || fb.volatileThis);
            const bool bWithinA = (!fb.constThis || fa.constThis) && (!fb.volatileThis || fa.volatileThis);
            if (aWithinB && !bWithinA) better = true;
            if (bWithinA && !aWithinB) worse = true;
            continue;
        }
        if (a[i] == b[i]) {
            // [over.ics.rank]/3.3: user-defined sequences into one type meet at
            // one conversion, the reference binding after it decides: f("lit")
            // takes f(std::string &&), the converted value an rvalue (/3.2.3).
            const bool oneTarget = pa != nullptr && pb != nullptr && pa->isReference() && pb->isReference() &&
                                   pa->pointee()->unqualified() == pb->pointee()->unqualified();
            if (a[i] > Rank::Conversion &&
                !(oneTarget && a[i] >= Rank::UserDefined && a[i] <= Rank::UserDefinedConversion))
                continue;
            const int closer =
                closerTarget(pa, pb, i < rankingGiven_.size() ? rankingGiven_[i] : nullptr);
            if (closer > 0) better = true;
            if (closer < 0) worse = true;
            continue;
        }
        if (sameMatchEitherWay(pa, pb) &&
            a[i] <= Rank::Qualification && b[i] <= Rank::Qualification)
            continue;
        // [over.ics.rank]/3.3: a user-defined sequence is compared with another only through the
        // same conversion function; a class target was reached by a constructor, so with one on
        // either side the two stay level.
        if (a[i] >= Rank::UserDefined && a[i] <= Rank::UserDefinedConversion &&
            b[i] >= Rank::UserDefined && b[i] <= Rank::UserDefinedConversion) {
            const Type *va = pa != nullptr ? (pa->isReference() ? pa->pointee() : pa) : nullptr;
            const Type *vb = pb != nullptr ? (pb->isReference() ? pb->pointee() : pb) : nullptr;
            if (va == nullptr || vb == nullptr || va->unqualified()->isStructOrUnion() ||
                vb->unqualified()->isStructOrUnion())
                continue;
        }
        if (a[i] < b[i]) better = true;
        if (a[i] > b[i]) worse = true;
    }
    if (better && worse) return false;
    if (better) return true;
    if (worse) return false;
    // [over.ics.rank]/3.2.3: two ref-qualified members both viable means an rvalue object,
    // which `&&` binds as an rvalue reference where `const &` binds an lvalue one.
    if (ranksObjectA && ranksObjectB && fa.refThis != fb.refThis &&
        fa.refThis != RefQual::None && fb.refThis != RefQual::None)
        return fa.refThis == RefQual::RValue;
    if (fa.fromTemplate && fb.fromTemplate && fa.pattern != nullptr &&
        fb.pattern != nullptr)
        return moreSpecializedFunction(fa, fb);
    // [over.match.best]/1: an ordinary function before a template's specialization, a member template's too.
    const bool ta = fa.fromTemplate || fa.fromMemberTemplate, tb = fb.fromTemplate || fb.fromMemberTemplate;
    return !ta && tb;
}

bool Parser::moreSpecializedFunction(const Signature &a, const Signature &b) const {
    struct Order {
        static bool atLeast(const Parser &p, const Type *a, const Type *b) {
            const std::vector<const Type *> &pa = a->params(), &pb = b->params();
            // [temp.deduct.partial]/8: b's trailing function parameter pack takes each of a's parameters left
            // over, a's own pack included as its pattern; a pack of a's against a parameter of b's that is
            // none fails - `(integral_constant<int, 1>, allocator_arg_t, const A &)` before `(U &&, V &&...)`.
            const bool packB = !pb.empty() && pb.back()->kind() == Kind::PackExpansion;
            const std::size_t fixedB = packB ? pb.size() - 1 : pb.size();
            if (packB ? pa.size() < fixedB : pa.size() != pb.size()) return false;
            // b's parameters bound afresh; a's stand as the opaque types they are.
            std::vector<const Type *> binding(pb.size() + pa.size() + 64);
            std::string why;
            for (std::size_t i = 0; i < pa.size(); i++) {
                const Type *P = i < fixedB ? pb[i] : pb.back()->pointee(), *A = pa[i];
                if (A->kind() == Kind::PackExpansion) {
                    if (i < fixedB) return false;
                    A = A->pointee();
                }
                // [temp.deduct.partial]/5-7: a reference beside a value, or beside a reference of the other
                // kind, is compared by what it refers to, top-level cv dropped - `f(const F &)` against
                // `f(T *)`. Two references of one kind keep their cv, /9's tie-break.
                if (P->isReference() != A->isReference() ||
                    (P->isReference() && P->isRValueReference() != A->isRValueReference())) {
                    if (P->isReference()) P = P->referent();
                    if (A->isReference()) A = A->referent();
                    P = P->unqualified();
                    A = A->unqualified();
                }
                // Each element a pack takes is deduced on its own.
                std::vector<const Type *> fresh(binding.size());
                if (!p.matchPattern(P, A, i < fixedB ? &binding : &fresh, &why)) return false;
            }
            return true;
        }
    };
    const bool ab = Order::atLeast(*this, a.pattern, b.pattern);
    const bool ba = Order::atLeast(*this, b.pattern, a.pattern);
    if (ab != ba) return ab;
    if (!ab) return false;
    // [temp.deduct.partial]/9: each as specialized as the other, and an lvalue reference is more so than
    // an rvalue one - `k(const T &)` before `k(T &&)` for a const lvalue.
    const std::vector<const Type *> &pa = a.pattern->params(), &pb = b.pattern->params();
    bool more = false;
    for (std::size_t i = 0; i < pa.size() && i < pb.size(); i++) {
        if (!pa[i]->isReference() || !pb[i]->isReference()) continue;
        if (pa[i]->isRValueReference() && !pb[i]->isRValueReference()) return false;
        if (!pa[i]->isRValueReference() && pb[i]->isRValueReference()) more = true;
    }
    return more;
}

// [over.match.oper]: one candidate set with both halves in it - a member's implicit
// object parameter is what makes the two rank vectors comparable. This answers
// which half won, not which function. Then [basic.lookup.argdep], operands only.
std::vector<std::string> Parser::lookupKeys(const std::string &name,
                                            const Type *left,
                                            const Type *right) const {
    std::vector<std::string> keys;
    auto add = [&](const std::string &k) {
        if (!hasFunctionNamed(k)) return;
        for (std::size_t i = 0; i < keys.size(); i++)
            if (keys[i] == k) return;
        keys.push_back(k);
    };
    add(name);
    add(qualifyForLookup(name, &Parser::hasFunctionNamed));

    // [basic.lookup.argdep]/2: every namespace the operands are associated with - a class's, its bases'
    // and its template arguments', a specialization's template's (`N::K<false>` keeps N beside its tag),
    // and an enumeration's innermost one - so that `k * k` reaches N's operator* through a conversion.
    std::vector<std::string> spaces;
    associatedNamespaces(left, &spaces, 0);
    if (right != nullptr) associatedNamespaces(right, &spaces, 0);
    for (std::size_t i = 0; i < spaces.size(); i++) {
        if (spaces[i].empty()) continue;   // the global namespace's is the bare name's
        add(spaces[i] + "::" + name);
        // An inline namespace's class has the enclosing one too.
        const std::vector<std::string> alts = inlineAliasPrefixes(spaces[i] + "::");
        for (std::size_t a = 0; a < alts.size(); a++) add(alts[a] + name);
    }
    return keys;
}

bool Parser::hasVisibleFunctionNamed(const std::string &key) const {
    if (const std::vector<std::size_t> *s = overloadsOf(key))
        for (std::size_t i : *s)
            if (visibleBefore(functions_[i], visibleAt_)) return true;
    return false;
}

void Parser::associatedNamespaces(const Type *t, std::vector<std::string> *out, int depth) const {
    if (t == nullptr || depth > 16) return;
    while (t->isPointer() || t->isReference() || t->isArray()) t = t->pointee();
    t = t->unqualified();
    if (t->isFunction()) {
        associatedNamespaces(t->returns(), out, depth + 1);
        for (const Type *p : t->params()) associatedNamespaces(p, out, depth + 1);
        return;
    }
    const bool cls = t->isStructOrUnion() && !t->isMemberFunctionPointer();
    if ((!cls && !t->isEnumeration()) || t->tag().empty() || !t->localOwner().empty()) return;
    // The innermost namespace around it: the longest `::`-prefix of the tag outside any `<...>` that names one.
    const std::string &tag = t->tag();
    std::vector<std::size_t> cuts;
    for (std::size_t i = 0, angle = 0; i + 1 < tag.size(); i++) {
        if (tag[i] == '<') angle++;
        else if (tag[i] == '>' && angle > 0) angle--;
        else if (angle == 0 && tag[i] == ':' && tag[i + 1] == ':') cuts.push_back(i++);
    }
    std::string ns;
    for (std::size_t k = cuts.size(); k-- > 0; )
        if (namespaces_.count(tag.substr(0, cuts[k])) != 0) { ns = tag.substr(0, cuts[k]); break; }
    // A specialization keeps its template's namespace apart from its tag - `basic_string<char>` is std's.
    if (ns.empty() && t->isSpecialization() && t->templateNamespace().size() > 2 && cuts.empty())
        ns = t->templateNamespace().substr(0, t->templateNamespace().size() - 2);
    // A member class's: the class it is a member of is associated too, with that class's namespace.
    if (ns.empty() && cls && t->enclosing() != nullptr) associatedNamespaces(t->enclosing(), out, depth + 1);
    if (std::find(out->begin(), out->end(), ns) == out->end()) out->push_back(ns);
    if (!cls) return;
    for (const Type::BaseSpec &b : t->bases()) associatedNamespaces(b.type, out, depth + 1);
    for (const TemplateArg &a : t->templateArgs())
        if (a.isType && !a.isPack && !a.isTemplate) associatedNamespaces(a.type, out, depth + 1);
}

bool Parser::inDependentBase(const Type *cls, const Type *owner) const {
    std::map<const Type *, std::vector<const Type *> >::const_iterator it = dependentBases_.find(cls->unqualified());
    if (it == dependentBases_.end()) return false;
    struct Walk {
        static bool reaches(const Type *from, const Type *to, int depth) {
            if (from->unqualified() == to->unqualified()) return true;
            if (depth > 32) return false;
            for (const Type::BaseSpec &s : from->unqualified()->bases())
                if (reaches(s.type, to, depth + 1)) return true;
            return false;
        }
    };
    for (const Type *b : it->second)
        if (Walk::reaches(b, owner, 0)) return true;
    return false;
}

Parser::OperatorChoice Parser::resolveOperator(const std::string &name,
                                               const Expr &left,
                                               const Expr *right,
                                               std::size_t pos) {
    // **Non-member operator templates**: deduce and instantiate each that
    // applies before ranking (`a + b` on a class template); only those lookup
    // reaches, [over.match.oper]/3, and only specializations deduced here.
    std::set<std::string> deduced;
    {
        std::vector<const Type *> argTypes;
        argTypes.push_back(deductionArgType(left));
        if (right != nullptr) argTypes.push_back(deductionArgType(*right));
        const auto all = fnTemplates_.find(name);
        if (all != fnTemplates_.end()) {
            std::vector<std::string> spaces;
            associatedNamespaces(left.type(), &spaces, 0);
            if (right != nullptr) associatedNamespaces(right->type(), &spaces, 0);
            const std::string here = namespacePrefix();
            auto reached = [&](const std::string &ns) {
                if (ns.empty() || here.compare(0, ns.size(), ns) == 0) return true;
                for (const std::string &u : usingNamespaces_)
                    if (ns == u + "::") return true;
                for (const std::string &a : spaces)
                    if (!a.empty() && ns == a + "::") return true;
                return false;
            };
            std::vector<TemplateDecl> visible;
            for (const TemplateDecl &d : all->second) {
                bool ok = reached(d.ns);
                // An inline namespace's members are its enclosing namespace's too.
                for (std::string ns = d.ns; !ok && ns.size() > 2; ) {
                    const std::string path = ns.substr(0, ns.size() - 2);
                    if (inlineNamespaces_.count(path) == 0) break;
                    const std::string::size_type cut = path.rfind("::");
                    ns = cut == std::string::npos ? std::string() : path.substr(0, cut + 2);
                    ok = reached(ns);
                }
                if (ok) visible.push_back(d);
            }
            instantiateViableTemplates(visible, argTypes, pos, &deduced);
        }
    }

    // **A member operator may be a template too**, `H * x` where operator* is
    // written `template <int VMAX> V<VMAX> operator*(const V<VMAX> &) const`.
    if (right != nullptr) {
        const Type *lp = left.type()->unqualified();
        if (lp->isStructOrUnion()) {
            std::vector<const Type *> one;
            one.push_back(deductionArgType(*right));
            instantiateViableMemberTemplates(declaringClassOf(lp, name), name, one, pos);
        }
    }

    // A unary operator is the same question with one operand: a member takes it as
    // its implicit object and writes no parameter, a non-member writes one. One
    // rank each way, so the comparison is the same comparison.
    const std::size_t written = right != nullptr ? 1u : 0u;
    std::vector<std::vector<Rank> > ranks;
    std::vector<std::size_t> which;      // index into functions_
    std::vector<bool> member;

    const Type *lt = left.type();
    const Type *plain = lt->unqualified();
    if (plain->isStructOrUnion()) {
        const Type *owner = findMemberOwner(plain, name);
        if (owner != nullptr) {
            const std::string key = owner->tag() + "::" + name;
            if (const std::vector<std::size_t> *set = overloadsOf(key))
                for (std::size_t k = 0; k < set->size(); k++) {
                    const Signature &f = functions_[(*set)[k]];
                    if (f.params.size() != written) continue;
                    // The object parameter binds like any other reference: exact
                    // where the constness agrees, a qualification conversion for a
                    // const member on a non-const object, no match the other way.
                    if (lt->isConst() && !f.constThis) continue;
                    if (lt->isVolatile() && !f.volatileThis) continue;
                    if (!refQualifierViable(f.refThis, f.constThis, objectIsLvalue(left))) continue;
                    // A member of a base binds the derived object by a derived-to-base Conversion,
                    // [over.match.funcs]/5 and [over.ics.ref]/1 - as the non-member's reference does.
                    std::vector<Rank> r;
                    r.push_back(owner->tag() != plain->tag()
                                    ? Rank::Conversion
                                    : lt->isConst() == f.constThis && lt->isVolatile() == f.volatileThis
                                    ? Rank::Identity : Rank::Qualification);
                    if (right != nullptr) {
                        Rank second = rankArgument(*right, f.params[0]);
                        if (second == Rank::None) continue;
                        r.push_back(second);
                    }
                    ranks.push_back(r);
                    which.push_back((*set)[k]);
                    member.push_back(true);
                }
        }
    }

    const std::vector<std::string> keys =
        lookupKeys(name, lt, right != nullptr ? right->type() : nullptr);
    for (std::size_t ki = 0; ki < keys.size(); ki++)
    if (const std::vector<std::size_t> *set = overloadsOf(keys[ki]))
        for (std::size_t k = 0; k < set->size(); k++) {
            // Two keys may reach one function - the bare name and its namespace's - and it is one candidate.
            if (std::find(which.begin(), which.end(), (*set)[k]) != which.end()) continue;
            const Signature &f = functions_[(*set)[k]];
            if (f.params.size() != written + 1) continue;
            if (f.fromTemplate && deduced.count(f.symbol) == 0) continue;
            Rank a = rankArgument(left, f.params[0]);
            if (a == Rank::None) continue;
            std::vector<Rank> r;
            r.push_back(a);
            if (right != nullptr) {
                Rank b = rankArgument(*right, f.params[1]);
                if (b == Rank::None) continue;
                r.push_back(b);
            }
            ranks.push_back(r);
            which.push_back((*set)[k]);
            member.push_back(false);
        }

    if (which.empty()) return OperatorChoice::None;

    rankingGiven_.clear();
    rankingGiven_.push_back(lt);
    if (right != nullptr) rankingGiven_.push_back(right->type());
    std::size_t best = 0;
    for (std::size_t k = 1; k < which.size(); k++)
        if (betterCandidate(ranks[k], ranks[best],
                            functions_[which[k]], functions_[which[best]],
                            member[k], member[best]))
            best = k;
    for (std::size_t k = 0; k < which.size(); k++) {
        if (k == best) continue;
        if (!betterCandidate(ranks[best], ranks[k],
                             functions_[which[best]], functions_[which[k]],
                             member[best], member[k])) {
            std::string why = "this use of '" + name + "' is ambiguous";
            for (std::size_t j = 0; j < which.size(); j++)
                why += std::string("\n    candidate: ") +
                       (member[j] ? "member " : "") +
                       describeSignature(functions_[which[j]]);
            src_.fail(pos, why);
        }
    }
    // **[over.match.oper]/3: the built-in operator is a candidate too** ([over.built]/12, for every pair of
    // promoted arithmetic types), and it is viable where each class operand has a conversion function to a
    // number. It takes a number as it stands, where the class's own operator may need a conversion for it:
    // with `Conv(int)`, `operator double() const` and `Conv operator+(const Conv &) const`, `c + 3` is the
    // member through `Conv(3)` or `double + int` through the conversion function - each the better for one
    // operand, so the call is ambiguous, as cl and clang have it (1.80: the member was taken).
    if (right != nullptr) {
        static const char *const kIntegerOnly[] = { "operator%", "operator&", "operator|", "operator^",
                                                    "operator<<", "operator>>" };
        static const char *const kAnyNumber[] = { "operator+", "operator-", "operator*", "operator/", "operator<",
                                                  "operator>", "operator<=", "operator>=", "operator==",
                                                  "operator!=" };
        bool integerOnly = false, arithmetic = false;
        for (const char *n : kIntegerOnly) if (name == n) integerOnly = arithmetic = true;
        for (const char *n : kAnyNumber) if (name == n) arithmetic = true;
        const Type *given[2] = { lt, right->type() };
        bool viable = arithmetic, worse = false, better = false;
        for (int i = 0; i < 2 && viable; i++) {
            const Type *g = given[i]->unqualified();
            auto number = [&](const Type *t) {
                return (t->isEnumeration() && !t->isScopedEnumeration()) ||
                       (integerOnly ? t->isInteger() : t->isArithmetic());
            };
            if (g->isStructOrUnion() && !g->isMemberFunctionPointer()) {
                bool converts = false;
                for (const Type *c = g; c != nullptr && !converts; c = c->base())
                    for (std::size_t k = 0; k < functions_.size() && !converts; k++) {
                        const Signature &f = functions_[k];
                        if (f.owner != c->tag() || !isConversionName(f.name) || !f.params.empty() || f.isExplicit)
                            continue;
                        if ((given[i]->isConst() && !f.constThis) || (given[i]->isVolatile() && !f.volatileThis))
                            continue;
                        const Type *gives = f.returns->isReference() ? f.returns->referent() : f.returns;
                        converts = number(gives->unqualified());
                    }
                if (!converts) viable = false;
                else if (static_cast<int>(ranks[best][i]) < static_cast<int>(Rank::UserDefined)) better = true;
            } else if (!number(g)) {
                viable = false;
            } else {
                const Rank builtIn = g->isEnumeration() || promote(g) != g ? Rank::Promotion : Rank::Identity;
                if (static_cast<int>(ranks[best][i]) > static_cast<int>(builtIn)) worse = true;
            }
        }
        if (viable && worse && better)
            src_.fail(pos, "this use of '" + name + "' is ambiguous\n    candidate: " +
                           (member[best] ? "member " : "") + describeSignature(functions_[which[best]]) +
                           "\n    candidate: the built-in operator, each class operand through its conversion "
                           "function to a number");
        if (viable && worse) return OperatorChoice::None;
    }
    if (!member[best]) operatorChosen_ = which[best];
    return member[best] ? OperatorChoice::Member : OperatorChoice::NonMember;
}

Parser::Signature Parser::resolveChosenOperator(const std::string &name, std::vector<ExprPtr> &args,
                                                std::size_t pos) {
    struct Erase {
        Parser *p;
        std::string key;
        ~Erase() { p->functionIndex_.erase(key); }
    } own{ this, ownKey("$operator:" + name) };
    functionIndex_[own.key] = std::vector<std::size_t>(1, operatorChosen_);
    return resolveOverloadBraced(own.key, args, pos);
}

// The fewest arguments a call may give: every parameter that has no default.
// [dcl.fct.default] requires the defaults to be a suffix, so this is a count
// and not a set.
std::size_t Parser::leastArguments(const Signature &f) const {
    std::map<std::string, std::vector<std::size_t> >::const_iterator it =
        defaultArgs_.find(f.symbol);
    if (it == defaultArgs_.end()) return f.params.size();
    std::size_t least = f.params.size();
    while (least > 0 && least <= it->second.size() && it->second[least - 1] != 0)
        least--;
    return least;
}

// **Each default is read again, here, at the call that left it out** -
// [dcl.fct.default]/9 evaluates it afresh every time. The caller's locals are put
// aside, the declaration's scope being what it is read in and not the call's.
void Parser::applyDefaults(Signature f, std::vector<ExprPtr> &args,
                           std::size_t pos) {
    if (args.size() >= f.params.size()) return;
    std::map<std::string, std::vector<std::size_t> >::const_iterator it =
        defaultArgs_.find(f.symbol);
    if (it == defaultArgs_.end()) return;

    const std::size_t resume = at_;
    std::vector<Local> hidden;
    hidden.swap(locals_);
    std::vector<std::size_t> starts;
    starts.swap(scopeStarts_);
    // **The enclosing function is hidden for the same reason the locals are.**
    // [dcl.fct.default]/5 reads a default in the scope of the declaration, so what
    // the expression declares belongs there - a lambda's closure type included.
    const std::string outerFunction = currentFunction_;
    const std::string outerFunctionName = currentFunctionName_;
    currentFunction_.clear();
    currentFunctionName_.clear();
    // **And the rest of the declaration's scope**, which the locals were only
    // part of.
    const std::vector<std::string> outerNs = namespaceStack_;
    std::map<std::string, std::vector<std::string> >::const_iterator ns =
        defaultArgNamespace_.find(f.symbol);
    if (ns != defaultArgNamespace_.end()) namespaceStack_ = ns->second;
    const Type *outerClass = currentClass_;
    if (!f.owner.empty()) {
        if (const Type *owner = findTypedef(f.owner)) currentClass_ = owner;
    }
    Bound held(this);
    std::vector<Shadow> &unbound = held.undo;
    std::map<std::string, DefaultBinding>::const_iterator bound = defaultArgBindings_.find(f.symbol);
    if (bound != defaultArgBindings_.end())
        bindTemplateParameters(bound->second.params, bound->second.binding, bound->second.values,
                               bound->second.packs, &unbound);
    for (std::size_t i = args.size(); i < f.params.size(); i++) {
        if (i >= it->second.size() || it->second[i] == 0)
            src_.fail(pos, "'" + f.name + "' has no default for parameter " +
                           std::to_string(i + 1));
        at_ = it->second[i];
        // `P p = {1, 2}`: copy-list-initialisation of the parameter, [dcl.fct.default]/1 with [dcl.init.list].
        if (peek().is("{")) {
            Init in = parseInitialiser();
            args.push_back(bracedArgumentFor(f.params[i], in, pos));
        } else {
            args.push_back(assign());
        }
    }
    held.release();
    at_ = resume;
    locals_.swap(hidden);
    scopeStarts_.swap(starts);
    currentFunction_ = outerFunction;
    currentFunctionName_ = outerFunctionName;
    namespaceStack_ = outerNs;
    currentClass_ = outerClass;
}

// [temp.inst]/3: an unevaluated operand needs the declaration, and the body is asked for by a use.
void Parser::refuseFailedBody(const Signature &chosen) const {
    if (unevaluated_ != 0) return;
    const auto f = failedBodies_.find(chosen.symbol);
    if (f != failedBodies_.end()) src_.fail(f->second.first, f->second.second);
}

// [basic.lookup.argdep]/4: the function templates of the associated namespaces are candidates too, made where they
// deduce from these arguments (1.82).
void Parser::instantiateAdlTemplates(const std::string &written, const std::vector<std::string> &spaces,
                                     const std::vector<ExprPtr> &args, std::size_t pos) {
    if (written.compare(0, 1, "$") == 0) return;
    const auto tpl = fnTemplates_.find(written);
    if (tpl == fnTemplates_.end()) return;
    std::vector<TemplateDecl> more;
    for (const std::string &ns : spaces)
        for (const TemplateDecl &d : tpl->second)
            if (!ns.empty() && d.ns == ns + "::") more.push_back(d);
    if (more.empty()) return;
    std::vector<const Type *> argTypes;
    for (const ExprPtr &a : args) argTypes.push_back(deductionArgType(*a));
    instantiateViableTemplates(more, argTypes, pos, nullptr);
}

Parser::Signature Parser::resolveOverload(const std::string &written,
                                          const std::vector<ExprPtr> &args,
                                          std::size_t pos,
                                          const Type *object, bool objectLvalue) {
    const bool convertingOnly = convertingOnly_;
    convertingOnly_ = false;
    refuseOverloadArguments(args, written);
    // An unqualified name inside a namespace names that namespace's function
    // if it has one - the enclosing scopes are tried from the innermost out,
    // then whatever a `using namespace` has opened.
    std::string name = object != nullptr
                     ? written
                     : qualifyForLookup(written, &Parser::hasFunctionNamed);
    // An argument's own namespace is searched too - `twice(s)` finds
    // `N::twice` when `s` is an `N::S`. Only where nothing else answered, so a
    // name that is in scope is never quietly outranked by a far one.
    if (object == nullptr && overloadsOf(name) == nullptr && !args.empty()) {
        const std::vector<std::string> keys =
            lookupKeys(written, args[0]->type(),
                       args.size() > 1 ? args[1]->type() : nullptr);
        if (!keys.empty()) name = keys.back();
    }
    // **[temp.res]/10 and [temp.dep.candidate]: an unqualified call sees the functions declared before
    // it.** A template's body is read where it is instantiated, so one declared since must not join the
    // set ordinary lookup makes; argument-dependent lookup adds its namespaces' functions from anywhere.
    std::vector<std::size_t> visible;
    const bool unqualified = object == nullptr && written.find("::") == std::string::npos &&
                             written.compare(0, 8, "operator") != 0;
    bool later = false;
    if (unqualified)
        if (const std::vector<std::size_t> *s = overloadsOf(name))
            for (std::size_t i : *s)
                if (!visibleBefore(functions_[i], pos)) later = true;
    // **A set a template call put together** (`$adl:swap`: ordinary lookup's, the specializations this call
    // deduced, and what argument-dependent lookup added) is kept as it is, less what ordinary lookup alone
    // would have found too late - 1.77: it was looked up again under its own made-up name, which no
    // namespace has, so `n::swap(E &, E &)` declared after <utility> never reached std::swap's callers.
    if (later && written.compare(0, 1, "$") == 0) {
        std::string bare = written.substr(written.rfind(':') + 1);      // `$adl:swap`, `$adl:swap#4`
        if (bare.find('#') != std::string::npos) bare.erase(bare.find('#'));
        std::vector<std::string> spaces;
        for (const ExprPtr &a : args) associatedNamespaces(a->type(), &spaces, 0);
        std::set<std::size_t> byArguments;
        for (const std::string &ns : spaces) {
            std::vector<std::string> keys(1, ns.empty() ? bare : ns + "::" + bare);
            if (ns.empty()) keys.push_back("::" + bare);
            else for (const std::string &a : inlineAliasPrefixes(ns + "::")) keys.push_back(a + bare);
            for (const std::string &k : keys)
                if (const std::vector<std::size_t> *s = overloadsOf(k)) byArguments.insert(s->begin(), s->end());
        }
        for (std::size_t i : *overloadsOf(name))
            if (visibleBefore(functions_[i], pos) || byArguments.count(i) != 0) visible.push_back(i);
        if (visible.empty())
            src_.fail(pos, "'" + bare + "' is not declared before this use, and argument-dependent "
                           "lookup finds none - [temp.res]/10 binds a name in a template where the template "
                           "is written, not where it is instantiated");
    } else if (later) {
        visibleAt_ = pos;
        const std::string seen = qualifyForLookup(written, &Parser::hasVisibleFunctionNamed);
        visibleAt_ = 0;
        if (const std::vector<std::size_t> *s = overloadsOf(seen))
            for (std::size_t i : *s)
                if (visibleBefore(functions_[i], pos)) visible.push_back(i);
        std::vector<std::string> spaces;
        for (const ExprPtr &a : args) associatedNamespaces(a->type(), &spaces, 0);
        // The associated namespaces' function templates from anywhere, as below: `using std::cos; cos(x.re())`
        // in ad::sin<T> reaches ad::cos<T>, declared after it, for an ad::Dual (1.82).
        instantiateAdlTemplates(written, spaces, args, pos);
        for (const std::string &ns : spaces)
            if (const std::vector<std::size_t> *s = overloadsOf(ns.empty() ? written : ns + "::" + written))
                for (std::size_t i : *s)
                    if (std::find(visible.begin(), visible.end(), i) == visible.end()) visible.push_back(i);
        if (visible.empty())
            src_.fail(pos, "'" + written + "' is not declared before this use, and argument-dependent "
                           "lookup finds none - [temp.res]/10 binds a name in a template where the template "
                           "is written, not where it is instantiated");
        name = seen;
    }
    // **A constructor template is a candidate too**: every one under this key that
    // deduces from the arguments is instantiated before the set is read.
    bool inherits = false;
    for (const auto &from : inheritFrom_) inherits = inherits || constructorKey(from.first) == name;
    if (memberTemplates_.count(name) != 0 || inherits) {
        std::vector<const Type *> argTypes;
        for (std::size_t i = 0; i < args.size(); i++) argTypes.push_back(deductionArgType(*args[i]));
        instantiateViableMemberTemplatesUnder(name, argTypes, pos);
    }
    const std::vector<std::size_t> *set = later ? &visible : overloadsOf(name);

    // **[namespace.udir]/2: a using-directive's names join the global namespace's, they do not hide them** -
    // `using namespace s;` at file scope and `twice(3.0)` sees `::twice(double)` beside `s::twice(const E &)`
    // (1.82: only s's were candidates). Where lookup reached the name through a directive.
    // A directive written in a namespace and naming a namespace of the global one puts its names in the global
    // namespace too, the nearest that encloses both: there they meet the global function (no namespace around the
    // call declared the name, or lookup would have stopped there).
    std::vector<std::size_t> withGlobal;
    if (unqualified && !later && set != nullptr && name != written) {
        bool viaDirective = false;
        for (const std::string &u : usingNamespaces_)
            viaDirective = viaDirective || (u.find("::") == std::string::npos &&
                                            followUsingDeclaration(u + "::" + written) == name);
        if (viaDirective) {
            withGlobal = *set;
            const std::size_t before = withGlobal.size();
            if (const std::vector<std::size_t> *g = overloadsOf(written))
                for (std::size_t i : *g)
                    if (!functions_[i].fromTemplate && functions_[i].owner.empty() &&
                        std::find(withGlobal.begin(), withGlobal.end(), i) == withGlobal.end())
                        withGlobal.push_back(i);
            if (const std::vector<std::size_t> *g = overloadsOf("::" + written))
                for (std::size_t i : *g)
                    if (std::find(withGlobal.begin(), withGlobal.end(), i) == withGlobal.end()) withGlobal.push_back(i);
            if (withGlobal.size() > before) set = &withGlobal;
        }
    }

    // **[basic.lookup.argdep]/2: the associated namespaces join ordinary lookup's set** - every argument's
    // class, its bases and its template arguments' classes - so `who(w)` for a `B::W<A::X>` finds A's `who`.
    // Not where ordinary lookup found a class member (/3). The global namespace is ordinary lookup's own
    // only where that lookup reached it: a name found in a namespace nearer the call hides the global one,
    // and a global class's own function is then found here (1.77: it was not, and `swp(c, c)` written in a
    // namespace that has a `swp` of its own never reached `::swp(C &, C &)`).
    std::vector<std::size_t> withAdl;
    // `using std::cos; cos(x)` in a block arrives spelled as the using-declaration's key, `ad::cos`: still an
    // unqualified call, whose arguments' namespaces are searched by its bare name (1.82: ad::cos<T> for an
    // ad::Dual was never reached from ad::sin<T>).
    std::string adlWritten = written;
    bool viaBlockUsing = false;
    if (object == nullptr && !unqualified && written.find("::") != std::string::npos) {
        const std::string bare = written.substr(written.rfind("::") + 2);
        if (blockUsingFunctions_.count(bare) != 0 && written == namespacePrefix() + bare) {
            viaBlockUsing = true;
            adlWritten = bare;
        }
    }
    if ((unqualified || viaBlockUsing) && !later && !args.empty()) {
        bool member = false;
        if (set != nullptr)
            for (std::size_t i : *set) member = member || !functions_[i].owner.empty();
        if (!member) {
            std::vector<std::string> spaces;
            for (const ExprPtr &a : args) associatedNamespaces(a->type(), &spaces, 0);
            // **Their function templates too** ([basic.lookup.argdep]/4), where ordinary lookup found an ordinary
            // function: `using lib::twice; twice(d)` reaches `ad::twice<T>` for an `ad::D<double>` (1.82: only
            // the specializations some other call had made were seen, and none had).
            if (set != nullptr) instantiateAdlTemplates(adlWritten, spaces, args, pos);
            if (set != nullptr) withAdl = *set;
            const std::size_t before = withAdl.size();
            for (const std::string &ns : spaces) {
                if (ns.empty()) {
                    if (name == adlWritten || name.compare(0, 1, "$") == 0) continue;
                    if (const std::vector<std::size_t> *s = overloadsOf(adlWritten))
                        for (std::size_t i : *s)
                            if (!functions_[i].fromTemplate && functions_[i].owner.empty() &&
                                visibleBefore(functions_[i], pos) &&
                                std::find(withAdl.begin(), withAdl.end(), i) == withAdl.end())
                                withAdl.push_back(i);
                    continue;
                }
                std::vector<std::string> keys(1, ns + "::" + adlWritten);
                const std::vector<std::string> alts = inlineAliasPrefixes(ns + "::");
                for (const std::string &a : alts) keys.push_back(a + adlWritten);
                for (const std::string &k : keys)
                    if (k != name)
                        if (const std::vector<std::size_t> *s = overloadsOf(k))
                            for (std::size_t i : *s)
                                if (std::find(withAdl.begin(), withAdl.end(), i) == withAdl.end()) withAdl.push_back(i);
            }
            if (withAdl.size() > before) set = &withAdl;
        }
    }

    // **For an operator the candidate set is the union, not the first scope
    // that answers.**
    std::vector<std::size_t> merged;
    if (object == nullptr && !args.empty() &&
        written.compare(0, 8, "operator") == 0) {
        const std::vector<std::string> keys =
            lookupKeys(written, args[0]->type(),
                       args.size() > 1 ? args[1]->type() : nullptr);
        for (std::size_t i = 0; i < keys.size(); i++)
            if (const std::vector<std::size_t> *s = overloadsOf(keys[i]))
                for (std::size_t k = 0; k < s->size(); k++) {
                    bool seen = false;
                    for (std::size_t j = 0; j < merged.size() && !seen; j++)
                        seen = merged[j] == (*s)[k];
                    if (!seen) merged.push_back((*s)[k]);
                }
        if (!merged.empty()) set = &merged;
    }

    // [namespace.memdef]/3: a function only a friend declaration has declared is found by argument-dependent
    // lookup alone, and arguments of nothing but fundamental types, pointers and arrays of them lead it nowhere.
    std::vector<std::size_t> declared;
    if (set != nullptr && !set->empty() && object == nullptr) {
        bool fundamental = true;
        for (std::size_t a = 0; a < args.size() && fundamental; a++) {
            const Type *t = args[a]->type();
            if (t != nullptr && t->isReference()) t = t->referent();
            while (t != nullptr && (t->isPointer() || t->isArray())) t = t->pointee();
            if (t == nullptr || !(t->unqualified()->isVoid() || (t->isArithmetic() && !t->isEnumeration())))
                fundamental = false;
        }
        if (fundamental) {
            for (std::size_t i : *set)
                if (!functions_[i].hiddenFriend) declared.push_back(i);
            if (declared.empty())
                src_.fail(pos, "'" + written + "' is declared only by a friend declaration, and no lookup but "
                               "the argument-dependent one finds it there - [namespace.memdef]/3; these arguments "
                               "have no class to lead to it, so declare it outside the class too");
            if (declared.size() != set->size()) set = &declared;
        }
    }

    if (set == nullptr) {
        // Only constructor templates, and none of them deduced from these arguments.
        if (memberTemplates_.count(name) != 0)
            src_.fail(pos, "no constructor template of '" + name.substr(0, name.rfind("::")) +
                           "' can be used with these " + std::to_string(args.size()) +
                           " argument(s)");
        // **`C(...)` where C is a class** is a temporary and not a call to a
        // function nobody declared; "no prototype" sends the reader after a
        // declaration never meant to exist. Live now a class is copied by value.
        if (const Type *cls = findTypedef(name))
            if (cls->isStructOrUnion())
                src_.fail(pos, "'" + name + "(...)' makes a temporary of type '" +
                               cls->describe() + "', which is not supported yet - "
                               "name a variable of that type and use that");
        src_.fail(pos, "'" + name + "' was not declared - a prototype must come first");
    }

    std::vector<std::size_t> viable;
    std::vector<std::vector<Rank> > ranks;
    // Set when only the object's constness stopped a candidate, for the message.
    bool droppedForConst = false, droppedForVolatile = false;
    bool droppedForRef = false;

    bool skippedExplicit = false;
    for (int pass = 0; pass < 2; pass++) {
        for (std::size_t k = 0; k < set->size(); k++) {
            const Signature &f = functions_[(*set)[k]];
            // The explicit constructors wait for a second pass, taken where nothing else is viable: the
            // caller then names the explicit one it would have been.
            if (convertingOnly && pass == 0 && f.isExplicit) { skippedExplicit = true; continue; }
            // A default makes the parameter optional, so the count a call may
            // give is a range rather than one number.
            if (f.variadic ? args.size() < leastArguments(f)
                           : (args.size() > f.params.size() ||
                              args.size() < leastArguments(f))) continue;

            // **The implicit object parameter goes first**, and ranking it is what
            // separates `get()` from `get() const`: exact where the constness agrees,
            // a qualification conversion where a const member takes a non-const object.
            std::vector<Rank> r;
            // [over.match.funcs]/4: a static member's implicit object parameter matches any object.
            if (object != nullptr && !needsThis(f)) {
                r.push_back(Rank::Identity);
            } else if (object != nullptr) {
                if (object->isConst() && !f.constThis) { droppedForConst = true; continue; }
                if (object->isVolatile() && !f.volatileThis) { droppedForVolatile = true; continue; }
                if (!refQualifierViable(f.refThis, f.constThis, objectLvalue)) { droppedForRef = true; continue; }
                r.push_back(object->isConst() == f.constThis && object->isVolatile() == f.volatileThis
                                ? Rank::Identity : Rank::Qualification);
            }

            const std::size_t first = r.size();
            r.resize(first + args.size(), Rank::Ellipsis);
            // Only the arguments written are ranked; the defaults are the same
            // expression for every candidate that has them and cannot separate
            // two of them.
            bool ok = true;
            for (std::size_t i = 0; i < args.size() && ok; i++) {
                if (i >= f.params.size()) continue;      // reached by the ellipsis
                r[first + i] = rankArgument(*args[i], f.params[i]);
                if (r[first + i] == Rank::None) ok = false;
            }
            if (!ok) continue;
            viable.push_back((*set)[k]);
            ranks.push_back(r);
        }
        if (!viable.empty() || !skippedExplicit) break;
    }

    if (viable.empty() && droppedForVolatile)
        src_.fail(pos, "'" + name + "' is not a volatile member function, and this object is "
                       "volatile - [over.match.funcs]/4 binds a volatile object's 'this' to a "
                       "volatile member function alone");
    if (viable.empty() && droppedForConst)
        src_.fail(pos, "'" + name + "' is not a const member function, and this "
                       "object is const - calling it could change what the "
                       "const promised not to");
    if (viable.empty() && droppedForRef) src_.fail(pos, refQualifierRefusal(name, objectLvalue));

    if (viable.empty()) {
        std::string why = "no function called '" + name + "' takes these " +
                          std::to_string(args.size()) + " argument(s)";
        for (std::size_t k = 0; k < set->size(); k++)
            why += "\n    candidate: " + describeSignature(functions_[(*set)[k]]);
        src_.fail(pos, why);
    }
    if (viable.size() == 1) {
        refuseDeleted(functions_[viable[0]], pos);
        functions_[viable[0]].used = true;
        if (!functions_[viable[0]].isNoexcept) mayThrow_++;
        refuseFailedBody(functions_[viable[0]]);
        return functions_[viable[0]];
    }

    rankingGiven_.clear();
    if (object != nullptr) rankingGiven_.push_back(object);
    for (std::size_t i = 0; i < args.size(); i++) rankingGiven_.push_back(args[i]->type());
    std::size_t best = 0;
    for (std::size_t k = 1; k < viable.size(); k++)
        if (betterCandidate(ranks[k], ranks[best],
                            functions_[viable[k]], functions_[viable[best]],
                            object != nullptr, object != nullptr))
            best = k;
    for (std::size_t k = 0; k < viable.size(); k++) {
        if (k == best) continue;
        if (!betterCandidate(ranks[best], ranks[k],
                             functions_[viable[best]], functions_[viable[k]],
                             object != nullptr, object != nullptr)) {
            std::string why = "this call to '" + name + "' is ambiguous";
            for (std::size_t j = 0; j < viable.size(); j++)
                why += "\n    candidate: " + describeSignature(functions_[viable[j]]);
            src_.fail(pos, why);
        }
    }
    // [dcl.fct.def.delete]: a deleted candidate is ranked like any other and refused only where it wins.
    refuseDeleted(functions_[viable[best]], pos);
    functions_[viable[best]].used = true;
    // **Every call is potentially-throwing unless the function promised
    // otherwise**, and this is the one place a call finds out which function it
    // reached. `noexcept(e)` reads the count this leaves behind.
    if (!functions_[viable[best]].isNoexcept) mayThrow_++;
    refuseFailedBody(functions_[viable[best]]);
    return functions_[viable[best]];
}

void Parser::checkAssignable(const Expr &from, const Type *to, std::size_t pos,
                             const std::string &what) const {
    const Type *ft = from.type();

    // **[except.spec]/5: a pointer to function declared with an exception specification takes only
    // a function at least as restrictive** - `void (*p)() noexcept = g;` with g free to throw, or
    // `throw(int)` from a function with no specification. Asked of the declaration just read, once.
    if (fnPointerSpec_ != 0 && to->isPointer() && to->pointee()->isFunction()) {
        const int want = fnPointerSpec_;
        fnPointerSpec_ = 0;
        const Expr *e = &from;
        while (const Cast *c = dynamic_cast<const Cast *>(e)) e = &c->value();
        if (const Unary *u = dynamic_cast<const Unary *>(e))
            if (u->op() == '&') e = &u->operand();
        if (const Var *v = dynamic_cast<const Var *>(e))
            for (const Signature &f : functions_)
                if (f.symbol == v->symbol()) {
                    if ((want == 1 && !f.isNoexcept) || (want == 2 && !f.isNoexcept && !f.spec.written))
                        src_.fail(pos, what + " promises " +
                                       std::string(want == 1 ? "to throw nothing" : "a list of exceptions") +
                                       ", and '" + f.name + "' makes no such promise - [except.spec]/5 "
                                       "lets a pointer take only a function at least as restrictive");
                    break;
                }
    }

    // Copying ignores the cv at the top - but for a class, which copies through a constructor.
    if (ft->unqualified() == to->unqualified()) refuseVolatileClassCopy(ft, to, what, pos);
    if (ft == to) return;
    if (ft->unqualified() == to->unqualified()) return;

    if (ft->unqualified()->isScopedEnumeration()) refuseScopedConversion(ft, to, pos);
    if (ft->isArithmetic() && to->unqualified()->isEnumeration()) {
        // describe() spells an enumeration as its integer; this message is about the difference.
        auto shown = [](const Type *t) {
            if (t->unqualified()->isScopedEnumeration()) return "enum class " + t->unqualified()->enumTag();
            return t->unqualified()->isEnumeration() ? "enum " + t->unqualified()->enumTag()
                                                     : t->describe();
        };
        src_.fail(pos, what + " is '" + shown(to) + "' and this is '" + shown(ft) +
                       "' - C++ converts an integer to an enumeration only with a cast, "
                       "where C does it implicitly ([dcl.enum]/10)");
    }
    if (ft->isArithmetic() && to->isArithmetic()) return;
    if (memberPointerToDerived(ft, to) >= 0) return;

    // **A class converts by its own conversion function**, [class.conv.fct] - the mirror of the
    // converting constructor, and the same one rule: at most one user-defined conversion in a
    // sequence, so what the function answers is finished by a standard conversion and no further.
    if (ft->unqualified()->isStructOrUnion() &&
        ft->unqualified() != to->unqualified() &&
        !publiclyDerivedFrom(ft->unqualified(), to->unqualified()) &&
        const_cast<Parser *>(this)->conversionFunction(ft, to) != nullptr)
        return;
    // [class.conv.fct]/2: an `explicit` one is for direct-initialisation, a cast and a condition,
    // never for this copy - `bool b = f` (1.28, 2026-10-22, review of 1.25, neg07).
    if (ft->unqualified()->isStructOrUnion() && !to->unqualified()->isStructOrUnion() &&
        const_cast<Parser *>(this)->conversionFunction(ft, to, true) != nullptr)
        src_.fail(pos, what + " is '" + to->describe() + "' and this is '" + ft->describe() +
                       "', whose conversion function to it is 'explicit' - copy-initialisation "
                       "may not take it ([class.conv.fct]/2); write '" + to->describe() +
                       " x(v)', a static_cast, or use it as a condition");

    // Reached only by a lambda that captures: a captureless one has its conversion function (F22),
    // and [expr.prim.lambda]/6 gives one to no other.
    if (to->isPointer() && to->pointee()->isFunction() &&
        ft->unqualified()->isStructOrUnion() &&
        ft->unqualified()->tag().find("$_") != std::string::npos)
        src_.fail(pos, "a lambda converts to a function pointer only if it "
                       "captures nothing - call the closure directly, or give "
                       "the function a template parameter for it");

    // **[conv.ptr]/3, [class.member.lookup]: a base the derived class holds twice is ambiguous**, and
    // naming it converts to neither copy - `A *a = &d;` with `D : B, C`, both from A, took the first
    // (review of 1.30, A3).
    {
        const Type *fromCls = ft->isPointer() && to->isPointer() ? ft->pointee() : ft;
        const Type *toCls = ft->isPointer() && to->isPointer() ? to->pointee() : to;
        if (fromCls->unqualified()->isStructOrUnion() && toCls->unqualified()->isStructOrUnion() &&
            fromCls->unqualified() != toCls->unqualified() &&
            baseSubobjects(fromCls, toCls) > 1)
            src_.fail(pos, what + " is '" + to->describe() + "', and '" + toCls->unqualified()->describe() +
                           "' is an ambiguous base of '" + fromCls->unqualified()->describe() +
                           "' - it holds more than one, and this names none of them in particular; "
                           "convert to the intermediate base first ([conv.ptr]/3)");
    }
    refuseVolatileClassCopy(ft, to, what, pos);
    // Slicing, [class.copy]/12: a derived object copies to an accessible base that has no copy
    // constructor of its own as its base subobject's bytes; convert() takes the subobject.
    if (ft->unqualified()->isStructOrUnion() && to->unqualified()->isStructOrUnion() &&
        accessibleBaseOffset(ft->unqualified(), to->unqualified()) >= 0 &&
        const_cast<Parser *>(this)->copyConstructorOf(to->unqualified()) == nullptr &&
        const_cast<Parser *>(this)->moveConstructorOf(to->unqualified()) == nullptr)
        return;
    if (ft->unqualified()->isStructOrUnion() && to->unqualified()->isStructOrUnion() &&
        ft->unqualified() != to->unqualified() &&
        anyBaseOffset(ft->unqualified(), to->unqualified()) >= 0)
        src_.fail(pos, what + " is '" + to->describe() + "', a base of '" + ft->describe() +
                       "' that is not accessible here ([class.access.base]/4)");

    auto refuse = [&](const char *tail) {
        src_.fail(pos, what + " is '" + to->describe() + "' and this is '" +
                       ft->describe() + "'" + tail);
    };

    if (to->isPointer() && ft->isPointer()) {
        if (qualificationConvertible(ft, to)) return;
        // Derived * converts to Base *, the base being at offset 0, so the
        // value is unchanged and only the type moves.
        if (ft->pointee()->unqualified() != to->pointee()->unqualified() &&
            accessibleBaseOffset(ft->pointee(), to->pointee()) >= 0 &&
            (to->pointee()->isConst() || !ft->pointee()->isConst()) &&
            (to->pointee()->isVolatile() || !ft->pointee()->isVolatile()))
            return;
        // [conv.ptr]/2: an object pointer converts to void * implicitly, keeping its cv;
        // void * converts back only by a cast, which is where C++ parts from C.
        if (to->pointee()->isVoid() && !to->pointee()->isConst() &&
            ft->pointee()->isConst())
            refuse(" - 'void *' would drop the const; 'const void *' keeps it");
        if (to->pointee()->isVoid() && !to->pointee()->isVolatile() &&
            ft->pointee()->isVolatile())
            refuse(" - 'void *' would drop the volatile; 'volatile void *' keeps it");
        if (to->pointee()->isVoid()) return;
        if (ft->pointee()->isVoid())
            refuse(" - 'void *' converts to another pointer only with a cast in C++, where C allows it");
        // The commonest way to meet this rule is a C program handing a
        // string literal to a 'char *', so it is worth saying which rule
        // stopped it rather than leaving the reader to work back from const.
        if (isStringLiteral(from))
            refuse(" - a string literal is an array of const char in C++11, "
                   "and does not convert to a writable pointer");
        if (ft->pointee()->unqualified() == to->pointee()->unqualified() &&
            ft->pointee()->isVolatile() && !to->pointee()->isVolatile())
            refuse(" - the volatile would be dropped, and an access through it would no longer "
                   "be one the program asked for ([dcl.type.cv]/6)");
        if (ft->pointee()->unqualified() == to->pointee()->unqualified())
            refuse(" - the const would be dropped, and then the thing it "
                   "protects could be written through");
        if (anyBaseOffset(ft->pointee(), to->pointee()) > -1)
            refuse(" - the base is not accessible here, [class.access.base]/4: a private one only "
                   "inside the class and its friends");
        refuse(" - a cast says you meant it");
    }
    if (to->isPointer() && ft->isInteger()) {
        if (isNullConstant(from)) return;
        refuse(" - only the constant 0 becomes a pointer on its own");
    }
    // `nullptr` (and the constant 0, [conv.mem]/1) converts to any pointer or
    // pointer to member, function members too, and emits nothing: the value is
    // already a pointer-sized zero the front end knows is not the number 0.
    if (ft->isNullPtr() &&
        (to->isPointer() || to->isMemberPointer() || to->isMemberFunctionPointer())) return;
    if ((to->isMemberPointer() || to->isMemberFunctionPointer()) && ft->isInteger() &&
        isNullConstant(from)) return;
    if (to->isNullPtr()) {
        if (isNullConstant(from)) return;
        refuse(" - the only values of that type are 'nullptr' and the "
               "constant 0");
    }
    // [conv.bool]: a pointer or pointer to member converts to bool implicitly, null false and anything else true -
    // `bool b = p;` was refused (R4); to any other number only by a cast.
    if (to->unqualified()->isBool() && (ft->isPointer() || ft->isMemberPointer() || ft->isMemberFunctionPointer())) return;
    if (to->isArithmetic() && ft->isPointer())
        refuse(" - a pointer is not a number here, though a cast makes it one");

    refuse("");
}

