// The parser: conversions and overload resolution. The implicit conversion
// sequences of [conv] ranked as [over.ics] ranks them, the tournament that
// ranking decides, and the assignability check that asks the same questions.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

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
    if (t->isInteger() && t->rank() < types_.intType()->rank())
        return types_.intType();
    return t;
}

const Type *Parser::usualArithmetic(const Type *a, const Type *b) const {
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

ExprPtr Parser::convert(ExprPtr e, const Type *to, bool allowExplicit) const {
    if (e->type() == to) return e;

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
        !publiclyDerivedFrom(e->type()->unqualified(), to->unqualified())) {
        Parser *self = const_cast<Parser *>(this);
        if (const Signature *how = self->conversionFunction(e->type(), to, allowExplicit)) {
            const Type *object = e->type();
            std::vector<ExprPtr> none;
            return self->memberCallWith(std::move(e), object, how->name,
                                        0, std::move(none));
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
    // **A null data member pointer is -1 on both ABIs** - an offset of 0 is a
    // real member - and Microsoft's virtual model writes {0, -1}. It had been a
    // plain zero, so a member at 0 was null and a null from clang or cl was not.
    if (e->type()->isNullPtr() && to->unqualified()->isMemberPointer()) {
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
        const int off = publicBaseOffset(e->type()->pointee(), to->pointee());
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
            zero = convert(std::move(n), from);
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
    if (from == nullptr || !from->unqualified()->isStructOrUnion() ||
        !to->unqualified()->isStructOrUnion()) return;
    if (from->unqualified() == to->unqualified()) return;
    if (publicBaseOffset(from, to) > -1 || publicBaseOffset(to, from) > -1) return;
    if (conversionFunction(from, to->unqualified(), true) != nullptr) return;
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
    if (!e.type()->isScalar())
        src_.fail(pos, std::string(what) + " needs a number or a pointer, not '" +
                       e.type()->describe() + "'");
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
        const bool fromConst = cvOwner(from)->isConst(), toConst = cvOwner(to)->isConst();
        if (fromConst && !toConst) return false;
        if (!fromConst && toConst && !prefixConst) return false;
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

// The integral and floating promotions, [conv.prom] and [conv.fpprom], and
// only those - every other arithmetic pairing is a conversion, which ranks
// below. This is what makes f(int) beat f(double) for a char argument.
static bool isPromotion(const Type *from, const Type *to) {
    if (to->kind() == Kind::Int) {
        switch (from->kind()) {
            case Kind::Bool: case Kind::Char: case Kind::SChar: case Kind::UChar:
            case Kind::Short: case Kind::UShort: case Kind::WChar:
                return true;
            default:
                return false;
        }
    }
    return to->kind() == Kind::Double && from->kind() == Kind::Float;
}
Parser::Rank Parser::rankArgument(const Expr &arg, const Type *param) {
    const Type *given = arg.type();

    // A reference parameter binds or it does not; there is no conversion to
    // rank.
    if (param->isReference()) {
        const Type *want = param->pointee();

        // **A reference to an array binds to an array of the same length**,
        // without the decay a by-value parameter would apply.
        if (want->isArray() && given->isArray() && want->sameArrayShape(given)) {
            const Type *we = want->innermostElement();
            const Type *ge = given->innermostElement();
            if (we->unqualified() == ge->unqualified()) {
                if (!we->isConst() && ge->isConst()) return Rank::None;
                return we->isConst() && !ge->isConst() ? Rank::Qualification
                                                       : Rank::Identity;
            }
        }

        // **A reference to a base binds to a derived object** - [dcl.init.ref], and the sequence is
        // a derived-to-base Conversion, which is what makes `f(Base &)` lose to `f(Derived &)` for
        // a Derived rather than tie with it.
        if (want->unqualified() != given->unqualified() &&
            publicBaseOffset(given, want) > -1 &&
            isLvalue(arg) && !param->isRValueReference()) {
            if (!want->isConst() && given->isConst()) return Rank::None;
            return Rank::Conversion;
        }

        if (want->unqualified() != given->unqualified()) {
            // **A `const T &` takes a temporary, so it takes a conversion.**
            // [over.ics.user] with [dcl.init.ref]: the converting constructor
            // makes an object and the reference binds to it.
            if (want->isConst() && rankingConversion_ == 0) {
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
                if (usable) return Rank::UserDefined;
            }
            // **And a standard conversion makes that temporary too.**
            if (want->isConst()) {
                const Rank direct = rankArgument(arg, want->unqualified());
                if (direct != Rank::None) return direct;
            }
            return Rank::None;
        }
        if (!want->isConst() && given->isConst()) return Rank::None;

        // **Which reference will take this argument is a question about the
        // argument, not about a conversion.** An rvalue reference is not viable
        // for an object that has an address, and the better match where both are.
        if (param->isRValueReference() && isLvalue(arg)) return Rank::None;
        if (param->isRValueReference()) return Rank::Identity;

        // **A value has no lvalue to bind, so `T &` is not viable for one**.
        if (!isLvalue(arg)) return want->isConst() ? Rank::Qualification : Rank::None;

        return want->isConst() && !given->isConst() ? Rank::Qualification
                                                    : Rank::Identity;
    }

    const Type *from = decayedType(given);
    const Type *to = param;

    if (from == to) return Rank::Identity;
    // Top-level const on the parameter is not part of its type for this purpose.
    if (from->unqualified() == to->unqualified()) return Rank::Identity;

    if (from->isArithmetic() && to->isArithmetic())
        return isPromotion(from, to) ? Rank::Promotion : Rank::Conversion;

    if (to->isPointer() && from->isPointer()) {
        // A qualification conversion - char * to const char *.
        if (qualificationConvertible(from, to)) return Rank::Qualification;
        // Derived * to Base * is a pointer conversion, which ranks below a
        // promotion - so f(Base *) loses to f(Derived *) for a Derived *,
        // which is what [over.ics.rank] asks for.
        if (publiclyDerivedFrom(from->pointee(), to->pointee()) &&
            (to->pointee()->isConst() || !from->pointee()->isConst()))
            return Rank::Conversion;
        if (to->pointee()->isVoid() && !to->pointee()->isConst() &&
            from->pointee()->isConst())
            return Rank::None;
        if (to->pointee()->isVoid() || from->pointee()->isVoid())
            return Rank::Conversion;
        return Rank::None;
    }

    if (to->isPointer() && from->isInteger())
        return isNullConstant(arg) ? Rank::Conversion : Rank::None;

    // **`nullptr` converts to any pointer and to any pointer to member**, both
    // pointer conversions - so `f(void *)` and `f(char *)` are ambiguous for it, as
    // clang reports. **And not to bool here**: that conversion is direct-init only.
    if (from->isNullPtr() && (to->isPointer() || to->isMemberPointer()))
        return Rank::Conversion;
    if (memberPointerToDerived(from, to) > 0) return Rank::Conversion;

    // And the literal 0 converts *to* std::nullptr_t, at the same rank - which
    // is why `f(0)` against `f(decltype(nullptr))` and `f(char *)` is
    // ambiguous, measured.
    if (to->isNullPtr() && from->isInteger())
        return isNullConstant(arg) ? Rank::Conversion : Rank::None;

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
        if (usable) return Rank::UserDefined;
    }

    return Rank::None;
}

std::string Parser::describeSignature(const Signature &f) {
    std::string out = f.name + "(";
    for (std::size_t i = 0; i < f.params.size(); i++) {
        if (i > 0) out += ", ";
        out += f.params[i]->describe();
    }
    if (f.variadic) out += f.params.empty() ? "..." : ", ...";
    return out + ")";
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

bool Parser::betterCandidate(const std::vector<Rank> &a,
                             const std::vector<Rank> &b,
                             const Signature &fa, const Signature &fb,
                             bool ranksObjectA, bool ranksObjectB) const {
    bool better = false, worse = false;
    for (std::size_t i = 0; i < a.size() && i < b.size(); i++) {
        if (a[i] == b[i]) continue;
        if (sameMatchEitherWay(rankedParameter(fa, i, ranksObjectA),
                               rankedParameter(fb, i, ranksObjectB)) &&
            a[i] <= Rank::Qualification && b[i] <= Rank::Qualification)
            continue;
        if (a[i] < b[i]) better = true;
        if (a[i] > b[i]) worse = true;
    }
    if (better && worse) return false;
    if (better) return true;
    if (worse) return false;
    if (fa.fromTemplate && fb.fromTemplate && fa.pattern != nullptr &&
        fb.pattern != nullptr)
        return moreSpecializedFunction(fa, fb);
    return !fa.fromTemplate && fb.fromTemplate;
}

bool Parser::moreSpecializedFunction(const Signature &a, const Signature &b) const {
    struct Order {
        static bool atLeast(const Parser &p, const Type *a, const Type *b) {
            const std::vector<const Type *> &pa = a->params(), &pb = b->params();
            if (pa.size() != pb.size()) return false;
            // b's parameters bound afresh; a's stand as the opaque types they are.
            std::vector<const Type *> binding(pb.size() + pa.size());
            std::string why;
            for (std::size_t i = 0; i < pa.size(); i++)
                if (!p.matchPattern(pb[i], pa[i], &binding, &why)) return false;
            return true;
        }
    };
    return Order::atLeast(*this, a.pattern, b.pattern) &&
           !Order::atLeast(*this, b.pattern, a.pattern);
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

    const Type *operands[2] = { left, right };
    for (std::size_t i = 0; i < 2; i++) {
        const Type *t = operands[i];
        while (t != nullptr && (t->isPointer() || t->isReference()))
            t = t->pointee();
        if (t == nullptr) continue;
        t = t->unqualified();
        if (!t->isStructOrUnion() || !t->inNamespace()) continue;
        const std::string &tag = t->tag();
        const std::size_t cut = tag.rfind("::");
        if (cut != std::string::npos) add(tag.substr(0, cut) + "::" + name);
    }
    return keys;
}

Parser::OperatorChoice Parser::resolveOperator(const std::string &name,
                                               const Expr &left,
                                               const Expr *right,
                                               std::size_t pos) {
    // **A non-member operator may be a function template.** Deduce and
    // instantiate every one that applies before ranking, so `a + b` for a
    // class template reaches `template <int N> V<N> operator+(V<N>, V<N>)`.
    {
        std::vector<const Type *> argTypes;
        argTypes.push_back(left.type());
        if (right != nullptr) argTypes.push_back(right->type());
        instantiateViableTemplates(name, argTypes, pos);
    }

    // **A member operator may be a template too**, `H * x` where operator* is
    // written `template <int VMAX> V<VMAX> operator*(const V<VMAX> &) const`.
    if (right != nullptr) {
        const Type *lp = left.type()->unqualified();
        if (lp->isStructOrUnion()) {
            std::vector<const Type *> one;
            one.push_back(right->type());
            instantiateViableMemberTemplates(lp, name, one, pos);
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
                    std::vector<Rank> r;
                    r.push_back(lt->isConst() == f.constThis ? Rank::Identity
                                                             : Rank::Qualification);
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
            const Signature &f = functions_[(*set)[k]];
            if (f.params.size() != written + 1) continue;
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
    return member[best] ? OperatorChoice::Member : OperatorChoice::NonMember;
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
    for (std::size_t i = args.size(); i < f.params.size(); i++) {
        if (i >= it->second.size() || it->second[i] == 0)
            src_.fail(pos, "'" + f.name + "' has no default for parameter " +
                           std::to_string(i + 1));
        at_ = it->second[i];
        args.push_back(assign());
    }
    at_ = resume;
    locals_.swap(hidden);
    scopeStarts_.swap(starts);
    currentFunction_ = outerFunction;
    currentFunctionName_ = outerFunctionName;
    namespaceStack_ = outerNs;
    currentClass_ = outerClass;
}

Parser::Signature Parser::resolveOverload(const std::string &written,
                                          const std::vector<ExprPtr> &args,
                                          std::size_t pos,
                                          const Type *object) {
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
    const std::vector<std::size_t> *set = overloadsOf(name);

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

    if (set == nullptr) {
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
    bool droppedForConst = false;

    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = functions_[(*set)[k]];
        // A default makes the parameter optional, so the count a call may
        // give is a range rather than one number.
        if (f.variadic ? args.size() < leastArguments(f)
                       : (args.size() > f.params.size() ||
                          args.size() < leastArguments(f))) continue;

        // **The implicit object parameter goes first**, and ranking it is what
        // separates `get()` from `get() const`: exact where the constness agrees,
        // a qualification conversion where a const member takes a non-const object.
        std::vector<Rank> r;
        if (object != nullptr) {
            if (object->isConst() && !f.constThis) { droppedForConst = true; continue; }
            r.push_back(object->isConst() == f.constThis ? Rank::Identity
                                                         : Rank::Qualification);
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

    if (viable.empty() && droppedForConst)
        src_.fail(pos, "'" + name + "' is not a const member function, and this "
                       "object is const - calling it could change what the "
                       "const promised not to");

    if (viable.empty()) {
        std::string why = "no function called '" + name + "' takes these " +
                          std::to_string(args.size()) + " argument(s)";
        for (std::size_t k = 0; k < set->size(); k++)
            why += "\n    candidate: " + describeSignature(functions_[(*set)[k]]);
        src_.fail(pos, why);
    }
    if (viable.size() == 1) {
        functions_[viable[0]].used = true;
        if (!functions_[viable[0]].isNoexcept) mayThrow_++;
        return functions_[viable[0]];
    }

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
    functions_[viable[best]].used = true;
    // **Every call is potentially-throwing unless the function promised
    // otherwise**, and this is the one place a call finds out which function it
    // reached. `noexcept(e)` reads the count this leaves behind.
    if (!functions_[viable[best]].isNoexcept) mayThrow_++;
    return functions_[viable[best]];
}

void Parser::checkAssignable(const Expr &from, const Type *to, std::size_t pos,
                             const std::string &what) const {
    const Type *ft = from.type();

    if (ft == to) return;

    // Copying ignores the const at the top.
    if (ft->unqualified() == to->unqualified()) return;

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

    // **A lambda with no capture converts to a function pointer** -
    // [expr.prim.lambda]/6 gives the closure a conversion function returning
    // one that calls the body.
    if (to->isPointer() && to->pointee()->isFunction() &&
        ft->unqualified()->isStructOrUnion() &&
        ft->unqualified()->tag().find("$_") != std::string::npos)
        src_.fail(pos, "a lambda converts to a function pointer only if it "
                       "captures nothing, and that conversion is not "
                       "supported yet - call the closure directly, or give "
                       "the function a template parameter for it");

    auto refuse = [&](const char *tail) {
        src_.fail(pos, what + " is '" + to->describe() + "' and this is '" +
                       ft->describe() + "'" + tail);
    };

    if (to->isPointer() && ft->isPointer()) {
        if (qualificationConvertible(ft, to)) return;
        // Derived * converts to Base *, the base being at offset 0, so the
        // value is unchanged and only the type moves.
        if (publiclyDerivedFrom(ft->pointee(), to->pointee()) &&
            (to->pointee()->isConst() || !ft->pointee()->isConst()))
            return;
        // An implicit conversion through void * is C's rule, kept here and
        // recorded in docs/CONFORMANCE.md - but it must not become the way
        // round const that the rule above just closed.
        if (to->pointee()->isVoid() && !to->pointee()->isConst() &&
            ft->pointee()->isConst())
            refuse(" - 'void *' would drop the const; 'const void *' keeps it");
        if (to->pointee()->isVoid() || ft->pointee()->isVoid()) return;
        // The commonest way to meet this rule is a C program handing a
        // string literal to a 'char *', so it is worth saying which rule
        // stopped it rather than leaving the reader to work back from const.
        if (isStringLiteral(from))
            refuse(" - a string literal is an array of const char in C++11, "
                   "and does not convert to a writable pointer");
        if (ft->pointee()->unqualified() == to->pointee()->unqualified())
            refuse(" - the const would be dropped, and then the thing it "
                   "protects could be written through");
        refuse(" - a cast says you meant it");
    }
    if (to->isPointer() && ft->isInteger()) {
        if (isNullConstant(from)) return;
        refuse(" - only the constant 0 becomes a pointer on its own");
    }
    // `nullptr` converts to any pointer and to any pointer to member, and nothing
    // is emitted: the value is already a pointer-sized zero, and what the type
    // carried was the front end's knowledge that this zero is not the number 0.
    if (ft->isNullPtr() && (to->isPointer() || to->isMemberPointer())) return;
    if (to->isNullPtr()) {
        if (isNullConstant(from)) return;
        refuse(" - the only values of that type are 'nullptr' and the "
               "constant 0");
    }
    if (to->isArithmetic() && ft->isPointer())
        refuse(" - a pointer is not a number here, though a cast makes it one");

    refuse("");
}

