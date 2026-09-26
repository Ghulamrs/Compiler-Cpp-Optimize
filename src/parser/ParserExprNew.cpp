// The parser: making and destroying objects in an expression. `new` and `delete`
// and the allocator calls behind them, `throw` and the Microsoft form of it, and
// the temporaries a class-typed expression needs, the value-init zeroing included.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>


// `P(1)` - a temporary of class type. **The object goes in a slot of this frame
// and the expression answers with its name**, the constructor sequenced in front;
// it dies at the end of the full expression, which is what `pendingTemps_` is for.
ExprPtr Parser::pathAccess(ExprPtr root, const std::vector<InitStep> &path) {
    ExprPtr e = std::move(root);
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

// **Zero every scalar leaf of `type` reachable from `root`**, one store per leaf
// in declaration order. The root is whatever names the object and is copied for
// each leaf with clonePure, so it has to be pure; it used to be a slot number.
void Parser::zeroLeaves(const Expr &root, const Type *type,
                        std::vector<InitStep> &path,
                        std::vector<ExprPtr> &out) {
    if (type->isArray()) {
        const Type *elem = type->pointee();
        for (long long i = 0; i < type->length(); i++) {
            path.push_back(InitStep{ nullptr, i });
            zeroLeaves(root, elem, path, out);
            path.pop_back();
        }
        return;
    }
    if (type->isStructOrUnion()) {
        const std::vector<Member> &members = type->members();
        // A union is zeroed through its first member, which is what
        // [dcl.init]/8 asks for and what initZero already does.
        const std::size_t count = type->kind() == Kind::Union
                                ? (members.empty() ? std::size_t(0) : std::size_t(1))
                                : members.size();
        for (std::size_t i = 0; i < count; i++) {
            if (members[i].name.empty()) continue;
            path.push_back(InitStep{ &members[i], 0 });
            zeroLeaves(root, members[i].type, path, out);
            path.pop_back();
        }
        return;
    }
    ExprPtr z;
    if (type->isFloating()) { z.reset(new Num(0.0L)); z->setType(types_.doubleType()); }
    else                    { z.reset(new Num(0LL));  z->setType(types_.intType()); }
    ExprPtr again = clonePure(root);
    if (again == nullptr)
        src_.fail(0, "internal: value-initialisation was asked to zero an "
                     "object it cannot name twice");
    ExprPtr target = pathAccess(std::move(again), path);
    ExprPtr store(new Assign(std::move(target), convert(std::move(z), type)));
    store->setType(type);
    out.push_back(std::move(store));
}

// The stores zeroLeaves makes, chained by commas into one expression - or
// nullptr when there is no leaf to set, which is what an empty class is.
ExprPtr Parser::zeroChain(const Expr &root, const Type *type) {
    std::vector<ExprPtr> zeros;
    std::vector<InitStep> path;
    zeroLeaves(root, type->unqualified(), path, zeros);
    if (zeros.empty()) return nullptr;
    ExprPtr chain = std::move(zeros[0]);
    for (std::size_t i = 1; i < zeros.size(); i++) {
        const Type *t = zeros[i]->type();
        ExprPtr next(new Comma(std::move(chain), std::move(zeros[i])));
        next->setType(t);
        chain = std::move(next);
    }
    return chain;
}

ExprPtr Parser::classTemporary(const Type *cls, std::size_t pos) {
    const Type *plain = cls->unqualified();
    std::vector<ExprPtr> args;
    bool builtArgs = false;

    // **A braced list as the argument of a temporary**, `Row({1, 2})` -
    // [over.match.list]'s pick, made where only an expression fits.
    if (peek().is("{")) {
        const Type *ilType = nullptr;
        if (initializerListConstructor(plain, &ilType) != nullptr) {
            std::vector<StmtPtr> setup;
            Init in = parseInitialiser();
            ExprPtr list = buildInitializerList(ilType, in, pos, setup);
            expect(")");
            ExprPtr chain;
            for (std::size_t i = 0; i < setup.size(); i++) {
                ExprStmt *one = dynamic_cast<ExprStmt *>(setup[i].get());
                if (one == nullptr)
                    src_.fail(pos, "this braced list needs setup an expression "
                                   "cannot carry - name a variable of type '" +
                                   plain->describe() + "' and initialise it "
                                   "with the braces instead");
                ExprPtr e = one->release();
                const Type *t = e->type();
                if (chain == nullptr) { chain = std::move(e); continue; }
                ExprPtr joined(new Comma(std::move(chain), std::move(e)));
                joined->setType(t);
                chain = std::move(joined);
            }
            if (chain != nullptr) {
                const Type *lt = list->type();
                ExprPtr seq(new Comma(std::move(chain), std::move(list)));
                seq->setType(lt);
                list = std::move(seq);
            }
            args.push_back(std::move(list));
            builtArgs = true;      // the ')' is consumed above
        }
    }
    if (!builtArgs) parseArguments(args);

    const std::string key = constructorKey(plain->tag());
    if (overloadsOf(key) != nullptr)
        convertThroughConversionFunction(args, plain, true, pos);
    // **`T(t)` for a class whose constructors leave its copy trivial** is a
    // copy of bytes too: no copy constructor was declared, so resolution had
    // nothing to find and `T b = T(T::make())` was refused.
    const bool trivialSameClass =
        args.size() == 1 && args[0]->type() != nullptr &&
        args[0]->type()->unqualified() == plain &&
        copyConstructorOf(plain) == nullptr && moveConstructorOf(plain) == nullptr;
    if (overloadsOf(key) == nullptr || trivialSameClass) {
        // No constructor at all: `P(x)` is then a copy of another P, which is
        // a move of bytes, and `P()` is an object with nothing to set.
        if (args.size() > 1)
            src_.fail(pos, "'" + plain->describe() + "' has no constructor, so "
                           "'" + plain->tag() + "(...)' can only be a copy of "
                           "another one - and this gives " +
                           std::to_string(args.size()) + " arguments");
        const int slot = allocateFrameSlot(plain);
        if (destructorOf(plain) != nullptr)
            pendingTemps_.push_back(Temporary{ slot, plain, 0 });
        ExprPtr obj(Var::local("$tmp", slot));
        obj->setType(plain);
        // Expiring, whichever of the three shapes below answers - the same
        // mark the constructor branch sets, for the same reason.
        obj->setXvalue();
        if (args.empty()) {
            // **[dcl.init]/8: `P()` value-initialises, and for a class with no
            // user-provided constructor that means zeroing it.** The slot was
            // handed back as it stood, so `f(P())` read whatever the frame held.
            ExprPtr chain = zeroChain(*obj, plain);
            if (chain == nullptr) return obj;
            // Comma'd with the object's *address* and dereferenced, which is
            // the shape the argument path below already uses: a comma whose
            // value is a class lvalue is not something every backend spells.
            ExprPtr again(Var::local("$tmp", slot));
            again->setType(plain);
            ExprPtr at(new Unary('&', std::move(again)));
            at->setType(types_.pointerTo(plain));
            ExprPtr both(new Comma(std::move(chain), std::move(at)));
            both->setType(types_.pointerTo(plain));
            ExprPtr made(new Unary('*', std::move(both)));
            made->setType(plain);
            made->setXvalue();
            return made;
        }
        checkAssignable(*args[0], plain, pos, "this temporary");
        ExprPtr store(new Assign(std::move(obj), std::move(args[0])));
        store->setType(plain);
        ExprPtr again(Var::local("$tmp", slot));
        again->setType(plain);
        ExprPtr at(new Unary('&', std::move(again)));
        at->setType(types_.pointerTo(plain));
        ExprPtr both(new Comma(std::move(store), std::move(at)));
        both->setType(types_.pointerTo(plain));
        ExprPtr made(new Unary('*', std::move(both)));
        made->setType(plain);
        made->setXvalue();
        return made;
    }

    // **Read before the argument list is touched.** [dcl.init]/8's other half: a
    // class whose default constructor nobody wrote is zeroed *and then* built,
    // where a user-provided one gets no zeroing. The same paragraph says both.
    const bool valueInit = args.empty();
    const Signature &ctor = resolveOverload(key, args, pos);
    applyDefaults(ctor, args, pos);
    if (ctor.access != Access::Public &&
        !insideAccessOf(plain, ctor.access) && !isFriendOf(plain))
        src_.fail(pos, "'" + plain->describe() + "' has no public constructor "
                       "taking these arguments - the one that matches is " +
                       (ctor.access == Access::Private ? "private" : "protected"));
    const bool zeroFirst = valueInit && ctor.implicit;
    return constructTemporary(plain, ctor, std::move(args), zeroFirst, pos);
}

// **The conversion function on `from` that answers something usable.** The
// mirror of the converting constructor: that one is a constructor of the
// *target*, this one a member of the *source*.
const Parser::Signature *Parser::conversionFunction(const Type *from,
                                                    const Type *to,
                                                    bool allowExplicit) {
    const Type *plain = from->unqualified();
    if (!plain->isStructOrUnion()) return nullptr;

    // **Walked over the signature table and not over a member list**, because there is no member
    // list: the table is keyed by `Class::name`, so a class's conversions cannot be asked for
    // without already knowing what they convert to.
    const Signature *best = nullptr;
    bool exact = false;
    for (const Type *c = plain; c != nullptr; c = c->base()) {
        for (std::size_t k = 0; k < functions_.size(); k++) {
            const Signature &f = functions_[k];
            if (f.owner != c->tag()) continue;
            if (!isConversionName(f.name) || !f.params.empty()) continue;
            if (f.isExplicit && !allowExplicit) continue;
            const Type *gives = f.returns;
            if (to == nullptr) {
                if (!gives->unqualified()->isScalar()) continue;
                if (gives->isBool()) return &f;          // the one asked for
                if (best != nullptr && !exact) return nullptr;
                if (best == nullptr) best = &f;
                continue;
            }
            if (gives->unqualified() == to->unqualified()) {
                if (exact) return nullptr;               // two exact answers
                best = &f;
                exact = true;
                continue;
            }
            if (exact) continue;
            if (!gives->unqualified()->isScalar() || !to->isScalar()) continue;
            if (best != nullptr) return nullptr;
            best = &f;
        }
    }
    return best;
}

// **The one conversion this class has to a number or a pointer**, or null when
// it has none or has more than one.
const Parser::Signature *Parser::soleNumericConversion(const Type *from) {
    const Type *plain = from->unqualified();
    if (!plain->isStructOrUnion()) return nullptr;
    const Signature *only = nullptr;
    for (const Type *c = plain; c != nullptr; c = c->base())
        for (std::size_t k = 0; k < functions_.size(); k++) {
            const Signature &f = functions_[k];
            if (f.owner != c->tag()) continue;
            if (!isConversionName(f.name) || !f.params.empty()) continue;
            if (!f.returns->unqualified()->isScalar()) continue;
            if (only != nullptr) return nullptr;
            only = &f;
        }
    return only;
}

// **A class where a number or a pointer is wanted**, converted by its own
// conversion function - a condition, `!`, `&&`, `||`, `?:`.
ExprPtr Parser::contextualScalar(ExprPtr e, std::size_t pos, const char *what) {
    if (e->type() != nullptr && e->type()->unqualified()->isStructOrUnion()) {
        if (const Signature *how = conversionFunction(e->type(), nullptr, true)) {
            const Type *object = e->type();
            std::vector<ExprPtr> none;
            e = memberCallWith(std::move(e), object, how->name, pos,
                               std::move(none));
        }
    }
    requireScalar(*e, pos, what);
    return e;
}

// **[over.ics.user]: the constructor that could make `to` out of `from`.**
const Parser::Signature *Parser::convertingConstructor(const Type *to,
                                                       const Expr &from) {
    const Type *plain = to->unqualified();
    if (!plain->isStructOrUnion()) return nullptr;
    const std::vector<std::size_t> *set = overloadsOf(constructorKey(plain->tag()));
    if (set == nullptr) return nullptr;

    const Signature *found = nullptr;
    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &c = functions_[(*set)[k]];
        if (c.isExplicit || c.params.size() != 1) continue;
        const Type *want = c.params[0];
        const Type *bare = want->isReference() ? want->pointee()->unqualified()
                                               : want->unqualified();
        if (bare == plain) continue;                 // the copy constructor
        if (rankArgument(from, want) == Rank::None) continue;
        if (found != nullptr) return nullptr;        // ambiguous, so neither
        found = &c;
    }
    return found;
}

// **The conversion an argument needs to become the parameter's class**, or
// null where none is needed or possible.
ExprPtr Parser::userConversion(const Type *param, ExprPtr &arg, std::size_t pos) {
    const Type *want = param->isReference() ? param->pointee() : param;
    const Type *plain = want->unqualified();
    if (!plain->isStructOrUnion()) return nullptr;
    if (param->isReference() && !want->isConst()) return nullptr;
    if (arg->type() == nullptr) return nullptr;
    if (arg->type()->unqualified() == plain) return nullptr;

    const Signature *ctor = convertingConstructor(plain, *arg);
    // **The other half of [over.ics.user]: a conversion function on the
    // argument's own class.** `take(const B &)` given an A with `operator B()`
    // was refused as no viable function. Copy-initialisation, so never explicit.
    if (ctor == nullptr) {
        if (!arg->type()->unqualified()->isStructOrUnion()) return nullptr;
        const Signature *how = conversionFunction(arg->type(), plain, false);
        if (how == nullptr) return nullptr;
        const Type *from = arg->type();
        std::vector<ExprPtr> none;
        return memberCallWith(std::move(arg), from, how->name, pos,
                              std::move(none));
    }
    markUsed(ctor);

    std::vector<ExprPtr> one;
    one.push_back(std::move(arg));
    return constructTemporary(plain, *ctor, std::move(one), false, pos);
}

// Whether any constructor of `cls` takes these arguments as they stand - the
// count and rank checks resolveOverload makes, without its refusal.
bool Parser::constructorViable(const Type *cls, const std::vector<ExprPtr> &args) {
    const std::vector<std::size_t> *set =
        overloadsOf(constructorKey(cls->unqualified()->tag()));
    for (std::size_t k = 0; set != nullptr && k < set->size(); k++) {
        const Signature &f = functions_[(*set)[k]];
        if (f.variadic ? args.size() < leastArguments(f)
                       : (args.size() > f.params.size() ||
                          args.size() < leastArguments(f))) continue;
        bool ok = true;
        for (std::size_t i = 0; i < args.size() && ok && i < f.params.size(); i++)
            if (rankArgument(*args[i], f.params[i]) == Rank::None) ok = false;
        if (ok) return true;
    }
    return false;
}

// **[over.match.copy]/1: direct-initialising a T from one argument of another
// class considers that class's conversion functions to T, the explicit ones
// included** - `CMatrix<3>(v)` reaching `explicit operator CMatrix<3, 3>()`.
bool Parser::convertThroughConversionFunction(std::vector<ExprPtr> &args,
                                              const Type *cls, bool directInit,
                                              std::size_t pos) {
    if (args.size() != 1 || args[0]->type() == nullptr) return false;
    const Type *plain = cls->unqualified();
    const Type *from = args[0]->type();
    if (!from->unqualified()->isStructOrUnion() || from->unqualified() == plain)
        return false;
    if (publicBaseOffset(from, plain) > -1) return false;
    if (constructorViable(plain, args)) return false;
    const Signature *how = conversionFunction(from, plain, directInit);
    if (how == nullptr) return false;
    std::vector<ExprPtr> none;
    args[0] = memberCallWith(std::move(args[0]), from, how->name, pos,
                             std::move(none));
    return true;
}

// **A temporary an arm of a `?:` made belongs to that arm.** The other arm did
// not build it, so nothing may destroy it - and a guard set at the end of the
// arm says so, being reached only if the arm ran.
ExprPtr Parser::markArmTemporaries(ExprPtr arm, std::size_t from,
                                   std::size_t to) {
    for (std::size_t k = from; k < to && k < pendingTemps_.size(); k++) {
        if (pendingTemps_[k].flag != 0) continue;
        if (destructorOf(pendingTemps_[k].type) == nullptr) continue;
        pendingTemps_[k].flag = guardFlag();
        ExprPtr mark(new Comma(std::move(arm),
                               setGuard(pendingTemps_[k].flag, 1)));
        mark->setType(types_.intType());
        arm = std::move(mark);
    }
    return arm;
}

// **The right operand of `&&` or `||` may not run at all**, and a temporary it
// would have built must not be destroyed when it did not.
ExprPtr Parser::markSkippableTemporaries(ExprPtr operand, std::size_t from,
                                         std::size_t to) {
    std::vector<std::size_t> needed;
    for (std::size_t k = from; k < to && k < pendingTemps_.size(); k++)
        if (pendingTemps_[k].flag == 0 &&
            destructorOf(pendingTemps_[k].type) != nullptr)
            needed.push_back(k);
    if (needed.empty()) return operand;

    const Type *vt = operand->type();
    const int slot = allocateFrameSlot(vt);
    ExprPtr into(Var::local("$sc", slot));
    into->setType(vt);
    ExprPtr chain(new Assign(std::move(into), std::move(operand)));
    chain->setType(vt);
    for (std::size_t i = 0; i < needed.size(); i++) {
        pendingTemps_[needed[i]].flag = guardFlag();
        ExprPtr mark(new Comma(std::move(chain),
                               setGuard(pendingTemps_[needed[i]].flag, 1)));
        mark->setType(types_.intType());
        chain = std::move(mark);
    }
    ExprPtr back(Var::local("$sc", slot));
    back->setType(vt);
    ExprPtr all(new Comma(std::move(chain), std::move(back)));
    all->setType(vt);
    return all;
}

// **Copy-initialise `cls` into a slot the caller owns**, as one expression of
// type int - the copy constructor where there is one, the bytes where the copy
// is trivial, which are the two answers a declaration gives.
ExprPtr Parser::buildInto(const Type *cls, int slot, ExprPtr value, int guard,
                          std::size_t pos) {
    const Type *ptr = types_.pointerTo(cls);
    if (ExprPtr made = userConversion(cls, value, pos)) value = std::move(made);

    ExprPtr built;
    const Signature *cc = copyConstructorOf(cls);
    const Signature *mv = moveConstructorOf(cls);
    const Signature *use = (!isLvalue(*value) && mv != nullptr) ? mv : cc;
    if (use != nullptr) {
        markUsed(use);
        ExprPtr where(Var::local("$cond", slot));
        where->setType(cls);
        ExprPtr at(new Unary('&', std::move(where)));
        at->setType(ptr);
        std::vector<ExprPtr> args;
        args.push_back(std::move(at));
        args.push_back(std::move(value));
        std::vector<const Type *> params;
        params.push_back(ptr);
        params.push_back(use->params[0]);
        built = completeCall(cls->tag(), use->symbol, nullptr,
                             types_.get(Kind::Void), params, false, pos,
                             std::move(args));
    } else {
        ExprPtr where(Var::local("$cond", slot));
        where->setType(cls);
        built.reset(new Assign(std::move(where), std::move(value)));
        built->setType(cls);
    }

    ExprPtr mark;
    if (guard != 0) mark.reset(new Comma(std::move(built), setGuard(guard, 1)));
    else {
        ExprPtr zero(new Num(0LL));
        zero->setType(types_.intType());
        mark.reset(new Comma(std::move(built), std::move(zero)));
    }
    mark->setType(types_.intType());
    return mark;
}

// **A temporary of `plain`, built by `ctor`, from arguments already parsed.**
ExprPtr Parser::constructTemporary(const Type *plain, const Signature &ctor,
                                   std::vector<ExprPtr> args, bool zeroFirst,
                                   std::size_t pos) {
    const int slot = allocateFrameSlot(plain);
    int guard = 0;
    if (destructorOf(plain) != nullptr) {
        guard = guardFlag();
        pendingTemps_.push_back(Temporary{ slot, plain, guard });
    }

    const Type *ptr = types_.pointerTo(plain);
    ExprPtr obj(Var::local("$tmp", slot));
    obj->setType(plain);
    ExprPtr addr(new Unary('&', std::move(obj)));
    addr->setType(ptr);

    std::vector<ExprPtr> all;
    all.push_back(std::move(addr));
    for (std::size_t i = 0; i < args.size(); i++) all.push_back(std::move(args[i]));
    std::vector<const Type *> full;
    full.push_back(ptr);
    for (std::size_t i = 0; i < ctor.params.size(); i++) full.push_back(ctor.params[i]);

    ExprPtr call = completeCall(plain->tag(), ctor.symbol, nullptr,
                                types_.get(Kind::Void), full, false, pos,
                                std::move(all));
    if (zeroFirst) {
        ExprPtr fresh(Var::local("$tmp", slot));
        fresh->setType(plain);
        if (ExprPtr chain = zeroChain(*fresh, plain)) {
            ExprPtr seq(new Comma(std::move(chain), std::move(call)));
            seq->setType(types_.get(Kind::Void));
            call = std::move(seq);
        }
    }

    // The object exists from the moment its constructor returns, and a cleanup
    // pad may run at any point after that - so the flag is set here rather
    // than the pad assuming the whole statement built everything it will.
    if (guard != 0) {
        ExprPtr mark(new Comma(std::move(call), setGuard(guard, 1)));
        mark->setType(types_.intType());
        call = std::move(mark);
    }

    // **A dereference of a pointer, not the object beside a comma.** `isGlvalue`
    // gives a comma its right operand's value category, so the parser would let
    // anyone take its address and no backend can. `*(ctor(&tmp), &tmp)` they know.
    ExprPtr again(Var::local("$tmp", slot));
    again->setType(plain);
    ExprPtr at(new Unary('&', std::move(again)));
    at->setType(ptr);
    ExprPtr both(new Comma(std::move(call), std::move(at)));
    both->setType(ptr);
    ExprPtr made(new Unary('*', std::move(both)));
    made->setType(plain);
    // **What `T(...)` makes is about to expire, and the marker is how the rest of
    // the compiler is told.** The lowering reads as an ordinary lvalue, so without
    // it a temporary passed by value reached for the copy constructor.
    made->setXvalue();
    return made;
}

// ---------------------------------------------------------------- new and delete
// **The four operator functions are called by name**, measured at -O0.
ExprPtr Parser::dynamicCastToVoid(ExprPtr v, const Type *to) {
    // **The Microsoft ABI keeps no offset-to-top, and does not do this
    // inline.**
    if (target_.microsoftNames()) {
        const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
        const int msSlot = allocateFrameSlot(voidPtr);
        const std::string msTemp = ".dyv" + std::to_string(newTemps_++);
        ExprPtr keep(Var::local(msTemp, msSlot));
        keep->setType(voidPtr);
        ExprPtr asVoid(new Cast(voidPtr, std::move(v)));
        asVoid->setType(voidPtr);
        ExprPtr store(new Assign(std::move(keep), std::move(asVoid)));
        store->setType(voidPtr);

        std::vector<ExprPtr> args;
        ExprPtr object(Var::local(msTemp, msSlot));
        object->setType(voidPtr);
        args.push_back(std::move(object));
        ExprPtr asked = runtimeCall("__RTCastToVoid", to, std::move(args));

        // The same null guard the pointer form uses. The runtime is documented
        // to answer null for null, but that is the half of this that clang did
        // not have to tell us, so it is not leaned on.
        ExprPtr noneMs(new Num(0LL));
        noneMs->setType(to);
        ExprPtr testMs(Var::local(msTemp, msSlot));
        testMs->setType(voidPtr);
        ExprPtr pickMs(new Conditional(std::move(testMs), std::move(asked),
                                       std::move(noneMs)));
        pickMs->setType(to);
        ExprPtr allMs(new Comma(std::move(store), std::move(pickMs)));
        allMs->setType(to);
        return allMs;
    }

    const Type *charPtr = types_.pointerTo(types_.get(Kind::Char));
    const Type *offsetType = ptrdiffType();
    const long long word = charPtr->size(target_);

    // The operand is read three times - tested, dereferenced for its vptr, and
    // added to - and it is any expression, so it goes into a slot first.
    const int slot = allocateFrameSlot(charPtr);
    const std::string temp = ".dyv" + std::to_string(newTemps_++);
    ExprPtr held(Var::local(temp, slot));
    held->setType(charPtr);
    ExprPtr asChar(new Cast(charPtr, std::move(v)));
    asChar->setType(charPtr);
    ExprPtr save(new Assign(std::move(held), std::move(asChar)));
    save->setType(charPtr);

    // `*(char **)p` - the vtable pointer.
    ExprPtr obj(Var::local(temp, slot));
    obj->setType(charPtr);
    ExprPtr asTable(new Cast(types_.pointerTo(charPtr), std::move(obj)));
    asTable->setType(types_.pointerTo(charPtr));
    ExprPtr vptr(new Unary('*', std::move(asTable)));
    vptr->setType(charPtr);

    // Two words in front of it, read as a signed offset. A raw `Binary` on a
    // pointer adds bytes - `pointerAdd` is what scales, and it is not used here
    // for exactly that reason.
    ExprPtr back(new Num(-2 * word));
    back->setType(offsetType);
    ExprPtr at(new Binary(BinOp::Add, std::move(vptr), std::move(back)));
    at->setType(charPtr);
    ExprPtr asOffset(new Cast(types_.pointerTo(offsetType), std::move(at)));
    asOffset->setType(types_.pointerTo(offsetType));
    ExprPtr offset(new Unary('*', std::move(asOffset)));
    offset->setType(offsetType);

    ExprPtr base(Var::local(temp, slot));
    base->setType(charPtr);
    ExprPtr moved(new Binary(BinOp::Add, std::move(base), std::move(offset)));
    moved->setType(charPtr);
    ExprPtr complete(new Cast(to, std::move(moved)));
    complete->setType(to);

    ExprPtr none(new Num(0LL));
    none->setType(to);
    ExprPtr test(Var::local(temp, slot));
    test->setType(charPtr);
    ExprPtr chosen(new Conditional(std::move(test), std::move(complete),
                                   std::move(none)));
    chosen->setType(to);
    ExprPtr whole(new Comma(std::move(save), std::move(chosen)));
    whole->setType(to);
    return whole;
}

ExprPtr Parser::dynamicCast(std::size_t pos) {
    expect("<");
    StorageClass sc;
    const Type *to = specifiers(&sc);
    to = declarator(to, true).type;
    if (!atClosingAngle())
        src_.fail(peek().pos, "expected '>' to close 'dynamic_cast<'");
    takeClosingAngle();
    expect("(");
    ExprPtr v = expr();
    expect(")");

    if (to->isReference())
        src_.fail(pos, "'dynamic_cast' to a reference is not supported yet - it "
                       "has no null to return, so a failure throws "
                       "'std::bad_cast', and there is no C++ standard library "
                       "here to throw it from; the pointer form works and "
                       "answers with a null");
    // **[expr.dynamic.cast]/7: `void *` is the other target, and it asks a different question.**
    const bool toVoid = to->isPointer() && to->pointee()->unqualified()->isVoid();

    if (!toVoid &&
        (!to->isPointer() || !to->pointee()->unqualified()->isStructOrUnion()))
        src_.fail(pos, "'dynamic_cast' casts to a pointer to a class or to "
                       "'void *', and '" + to->describe() + "' is neither");

    const Type *from = v->type();
    if (!from->isPointer() || !from->pointee()->unqualified()->isStructOrUnion())
        src_.fail(pos, "'dynamic_cast' needs a pointer to a class to ask about, "
                       "and this is '" + from->describe() + "'");

    const Type *source = from->pointee()->unqualified();
    // [expr.dynamic.cast]/6: the operand's class must be polymorphic, because
    // the answer is read out of the vtable and a class without one has nothing
    // to read.
    if (!source->polymorphic())
        src_.fail(pos, "'" + source->describe() + "' has no virtual function, "
                       "so an object of it carries nothing that says what it "
                       "really is - 'dynamic_cast' has nothing to ask");
    if (toVoid) return dynamicCastToVoid(std::move(v), to);

    const Type *target = to->pointee()->unqualified();
    // **[expr.dynamic.cast]/5: an upcast is not a question.**
    if (publicBaseOffset(source, target) >= 0)
        return convert(std::move(v), to);

    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));

    // The operand is wanted twice - once to test against null and once to hand
    // to the runtime - and it is any expression, so it goes into a slot and
    // both readers use that.
    const int slot = allocateFrameSlot(voidPtr);
    const std::string temp = ".dyn" + std::to_string(newTemps_++);
    ExprPtr held(Var::local(temp, slot));
    held->setType(voidPtr);
    ExprPtr save(new Assign(std::move(held), std::move(v)));
    save->setType(voidPtr);

    const bool ms = target_.microsoftNames();

    // **The name of the object that describes a class, on whichever ABI.**
    auto describe = [&](const Type *cls) {
        std::string sym;
        if (ms) {
            MicrosoftRtti names;
            std::string why;
            if (cls->bases().size() <= 1 &&
                microsoftClassRttiNames(cls, &names, &why))
                sym = names.descriptor;
        } else {
            sym = emitClassTypeInfo(cls, cls->tag(), pos);
        }
        if (sym.empty())
            src_.fail(pos, "'" + cls->describe() + "' has more than one base, "
                           "and describing that to the run time needs a shape "
                           "carrying every base's offset and flags - "
                           "__vmi_class_type_info on Itanium, a multiple-"
                           "inheritance hierarchy on Microsoft - which is not "
                           "supported yet. A single base works");
        return sym;
    };

    auto typeInfoAddress = [&](const std::string &sym) {
        Var *ti = Var::global(sym);
        ti->setSymbol(sym);
        ExprPtr ref(ti);
        ref->setType(types_.get(Kind::Char));
        ExprPtr addr(new Unary('&', std::move(ref)));
        addr->setType(voidPtr);
        return addr;
    };

    const std::string sourceName = describe(source);
    const std::string targetName = describe(target);

    std::vector<ExprPtr> args;
    ExprPtr object(Var::local(temp, slot));
    object->setType(voidPtr);
    args.push_back(std::move(object));

    // **The two runtimes take their arguments in a different order and a
    // different number**, which is the whole of the difference at the call.
    if (ms) {
        ExprPtr delta(new Num(0LL));
        delta->setType(types_.get(Kind::Int));
        args.push_back(std::move(delta));
    }
    args.push_back(typeInfoAddress(sourceName));
    args.push_back(typeInfoAddress(targetName));
    if (ms) {
        ExprPtr isReference(new Num(0LL));
        isReference->setType(types_.get(Kind::Int));
        args.push_back(std::move(isReference));
    } else {
        ExprPtr hint(new Num(-1LL));
        hint->setType(types_.get(Kind::LongLong));
        args.push_back(std::move(hint));
    }

    ExprPtr asked = runtimeCall(ms ? "__RTDynamicCast" : "__dynamic_cast",
                                to, std::move(args));

    ExprPtr none(new Num(0LL));
    none->setType(to);
    ExprPtr test(Var::local(temp, slot));
    test->setType(voidPtr);
    ExprPtr chosen(new Conditional(std::move(test), std::move(asked),
                                   std::move(none)));
    chosen->setType(to);

    ExprPtr whole(new Comma(std::move(save), std::move(chosen)));
    whole->setType(to);
    return whole;
}

ExprPtr Parser::runtimeCall(const char *symbol, const Type *returns,
                            std::vector<ExprPtr> args) {
    std::vector<int> argSlots(args.size(), 0);
    Call *call = new Call(symbol, nullptr, std::move(args), false, 0,
                          static_cast<int>(argSlots.size()),
                          std::move(argSlots));
    call->setSymbol(symbol);
    ExprPtr n(call);
    n->setType(returns);
    return n;
}

// **The Microsoft ABI throws from the stack, not from the heap**: `T tmp = x;` and
// then `_CxxThrowException(&tmp, &_TI1<letter>)`, where Itanium asks the runtime
// for memory. Identity is the ThrowInfo chain, four objects the backend emits.
void Parser::finishMicrosoftThrow(MicrosoftThrow &names, bool thrown) {
    names.thrown = thrown;
    for (std::size_t i = 0; i < names.catchables.size(); i++) {
        MicrosoftThrow::Catchable &c = names.catchables[i];
        if (c.cls == nullptr) continue;
        if (c.size == 0) c.size = c.cls->unqualified()->size(target_);
        // The runtime copies the caught object with this, so it is a use.
        if (const Signature *cc = copyConstructorOf(c.cls->unqualified())) {
            markUsed(cc);
            c.copyCtor = cc->symbol;
        }
    }
    if (!names.catchables.empty() && names.catchables[0].cls != nullptr)
        if (const Signature *d = destructorOf(names.catchables[0].cls->unqualified())) {
            markUsed(d);
            names.destructor = d->symbol;
        }
    microsoftThrowFinish(&names);
    for (std::size_t i = 0; i < current_->msThrows.size(); i++)
        if (current_->msThrows[i].info == names.info) {
            current_->msThrows[i].thrown |= thrown;
            return;
        }
    current_->msThrows.push_back(names);
}

StmtPtr Parser::microsoftThrow(ExprPtr value, std::size_t pos) {
    value = decay(std::move(value));
    const Type *thrown = value->type()->unqualified();
    MicrosoftThrow names;
    std::string why;
    if (!microsoftThrowNames(thrown, thrown->size(target_), &names, &why))
        src_.fail(pos, "'throw' cannot name the type of this: " + why);
    finishMicrosoftThrow(names, true);

    const int slot = allocateFrameSlot(thrown);
    const std::string temp = ".ex" + std::to_string(refTemps_++);
    ExprPtr held(Var::local(temp, slot));
    held->setType(thrown);
    // **[except.throw]/3 copy-initialises the exception object**: the copy
    // constructor where there is one, the bytes where there is not.
    ExprPtr store;
    const Signature *cc = thrown->isStructOrUnion() ? copyConstructorOf(thrown) : nullptr;
    if (cc != nullptr) {
        const Type *thrownPtr = types_.pointerTo(thrown);
        ExprPtr at(new Unary('&', std::move(held)));
        at->setType(thrownPtr);
        std::vector<ExprPtr> ctorArgs;
        ctorArgs.push_back(std::move(at));
        ctorArgs.push_back(convert(std::move(value), thrown));
        std::vector<const Type *> ps;
        ps.push_back(thrownPtr);
        ps.push_back(cc->params[0]);
        store = completeCall(thrown->tag(), cc->symbol, nullptr,
                             types_.get(Kind::Void), ps, false, pos,
                             std::move(ctorArgs));
    } else {
        store.reset(new Assign(std::move(held), convert(std::move(value), thrown)));
        store->setType(thrown);
    }

    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    std::vector<ExprPtr> args;
    ExprPtr object(Var::local(temp, slot));
    object->setType(thrown);
    ExprPtr address(new Unary('&', std::move(object)));
    address->setType(voidPtr);
    args.push_back(std::move(address));

    Var *ti = Var::global(names.info);
    ti->setSymbol(names.info);
    ExprPtr tiRef(ti);
    tiRef->setType(types_.get(Kind::Char));
    ExprPtr tiAddr(new Unary('&', std::move(tiRef)));
    tiAddr->setType(voidPtr);
    args.push_back(std::move(tiAddr));

    ExprPtr thrower = runtimeCall("_CxxThrowException", types_.get(Kind::Void),
                                  std::move(args));
    // The object is made, the operand's temporaries go, and then the throw -
    // [except.throw]/3's order, as the Itanium path keeps it.
    std::vector<StmtPtr> steps;
    steps.push_back(StmtPtr(new ExprStmt(std::move(store))));
    flushTemporaries(steps);
    steps.push_back(StmtPtr(new ExprStmt(std::move(thrower))));
    Block *b = new Block(std::move(steps));
    b->setScope(-1);
    return StmtPtr(b);
}

// **`throw x;` is three calls and a store, and no new machinery**:
// __cxa_allocate_exception, the store, __cxa_throw with the type that identifies
// it. That type_info pointer is the work - fundamental types, the rest refused.
StmtPtr Parser::throwStatement(ExprPtr value, std::size_t pos) {
    const Type *thrown = value->type()->unqualified();
    if (target_.microsoftNames()) return microsoftThrow(std::move(value), pos);

    std::string why;
    const std::string info = typeInfoSymbolFor(thrown, pos, &why);
    if (info.empty())
        src_.fail(pos, "'throw' cannot name the type of this: " + why);

    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    const int slot = allocateFrameSlot(voidPtr);
    const std::string temp = ".ex" + std::to_string(refTemps_++);

    std::vector<ExprPtr> sizeArg;
    ExprPtr howBig(new Num(static_cast<long long>(thrown->size(target_))));
    howBig->setType(types_.get(target_.sizeType()));
    sizeArg.push_back(std::move(howBig));
    ExprPtr got = runtimeCall("__cxa_allocate_exception", voidPtr,
                              std::move(sizeArg));

    ExprPtr held(Var::local(temp, slot));
    held->setType(voidPtr);
    ExprPtr save(new Assign(std::move(held), std::move(got)));
    save->setType(voidPtr);

    const Type *thrownPtr = types_.pointerTo(thrown);
    ExprPtr asT(Var::local(temp, slot));
    asT->setType(voidPtr);
    ExprPtr cast(new Cast(thrownPtr, std::move(asT)));
    cast->setType(thrownPtr);

    // **[except.throw]/3 copy-initialises the exception object**, and that
    // means the copy constructor where there is one.
    ExprPtr store;
    const Signature *cc = copyConstructorOf(thrown);
    if (cc != nullptr) {
        markUsed(cc);
        std::vector<ExprPtr> ctorArgs;
        ctorArgs.push_back(std::move(cast));
        ctorArgs.push_back(convert(std::move(value), thrown));
        std::vector<const Type *> ps;
        ps.push_back(thrownPtr);
        ps.push_back(cc->params[0]);
        store = completeCall(thrown->tag(), cc->symbol, nullptr,
                             types_.get(Kind::Void), ps, false, pos,
                             std::move(ctorArgs));
    } else {
        // No copy constructor is a trivially copyable class - or a fundamental
        // type - and the bytes are the copy, which is what this always did.
        ExprPtr into(new Unary('*', std::move(cast)));
        into->setType(thrown);
        store.reset(new Assign(std::move(into), convert(std::move(value), thrown)));
        store->setType(thrown);
    }

    // The exception object, the type that identifies it, and the destructor
    // it does not have. A fundamental type needs none, so the third argument
    // is the null the ABI asks for there.
    std::vector<ExprPtr> throwArgs;
    ExprPtr object(Var::local(temp, slot));
    object->setType(voidPtr);
    throwArgs.push_back(std::move(object));

    Var *ti = Var::global(info);
    ti->setSymbol(info);
    ExprPtr tiRef(ti);
    tiRef->setType(types_.get(Kind::Char));
    ExprPtr tiAddr(new Unary('&', std::move(tiRef)));
    tiAddr->setType(voidPtr);
    throwArgs.push_back(std::move(tiAddr));

    // **The third argument is the destructor the runtime runs on the exception
    // object**, and it was a null written when only fundamental types could be
    // thrown.
    const Signature *dtor = destructorOf(thrown);
    if (dtor != nullptr) {
        markUsed(dtor);
        Var *dv = Var::global(dtor->symbol);
        dv->setSymbol(dtor->symbol);
        ExprPtr dref(dv);
        dref->setType(types_.get(Kind::Char));
        ExprPtr daddr(new Unary('&', std::move(dref)));
        daddr->setType(voidPtr);
        throwArgs.push_back(std::move(daddr));
    } else {
        ExprPtr none(new Num(0LL));
        none->setType(voidPtr);
        throwArgs.push_back(std::move(none));
    }

    ExprPtr thrower = runtimeCall("__cxa_throw", types_.get(Kind::Void),
                                  std::move(throwArgs));

    // **Three statements, because something happens between them.** The
    // exception object is initialised, *then* the operand's temporaries are
    // destroyed - [except.throw]/3 - and only then does the throw leave.
    std::vector<StmtPtr> steps;
    const bool copyMayThrow = cc != nullptr && !cc->isNoexcept;
    int storageGuard = 0;
    if (copyMayThrow) {
        storageGuard = guardFlag();
        Temporary storage{ slot, voidPtr, storageGuard };
        storage.exceptionStorage = true;
        statementTemps_.push_back(storage);
    }
    ExprPtr first(new Comma(std::move(save), std::move(store)));
    first->setType(types_.get(Kind::Void));
    if (copyMayThrow)
        steps.push_back(StmtPtr(new ExprStmt(setGuard(storageGuard, 1))));
    steps.push_back(StmtPtr(new ExprStmt(std::move(first))));
    if (copyMayThrow)
        steps.push_back(StmtPtr(new ExprStmt(setGuard(storageGuard, 0))));
    flushTemporaries(steps);
    steps.push_back(StmtPtr(new ExprStmt(std::move(thrower))));
    Block *b = new Block(std::move(steps));
    b->setScope(-1);
    return StmtPtr(b);
}

ExprPtr Parser::callAllocator(const char *itanium, const char *microsoft,
                              const Type *returns, ExprPtr arg,
                              std::size_t pos) {
    (void)pos;
    std::vector<ExprPtr> args;
    args.push_back(std::move(arg));
    std::vector<int> argSlots(args.size(), 0);

    Call *call = new Call(target_.microsoftNames() ? microsoft : itanium,
                          nullptr, std::move(args), false, 0, 1,
                          std::move(argSlots));
    call->setSymbol(target_.microsoftNames() ? microsoft : itanium);
    ExprPtr n(call);
    n->setType(returns);
    return n;
}

// `operator new(size_t, const std::nothrow_t &)` and its array twin, by the
// names clang and cl agree on: _ZnwmRKSt9nothrow_t, ??2@YAPEAX_KAEBUnothrow_t@std@@@Z.
ExprPtr Parser::callNothrowAllocator(bool array, ExprPtr bytes, ExprPtr tag,
                                     std::size_t pos) {
    (void)pos;
    const std::string itanium = std::string(array ? "_Zna" : "_Znw") +
                                itaniumBuiltinCode(target_.sizeType()) + "RKSt9nothrow_t";
    const char *microsoft = array ? "??_U@YAPEAX_KAEBUnothrow_t@std@@@Z"
                                  : "??2@YAPEAX_KAEBUnothrow_t@std@@@Z";
    const Type *tagType = tag->type();
    ExprPtr addr(new Unary('&', std::move(tag)));
    addr->setType(types_.pointerTo(tagType));
    std::vector<ExprPtr> args;
    args.push_back(std::move(bytes));
    args.push_back(std::move(addr));
    std::vector<int> argSlots(args.size(), 0);
    const std::string name = target_.microsoftNames() ? microsoft : itanium;
    Call *call = new Call(name, nullptr, std::move(args), false, 0, 2, std::move(argSlots));
    call->setSymbol(name);
    ExprPtr n(call);
    n->setType(types_.pointerTo(types_.get(Kind::Void)));
    return n;
}

ExprPtr Parser::newExpression(std::size_t pos) {
    // **`new (p) T` builds T where p points** - [expr.new]/15 with the
    // library's placement `operator new(size_t, void *)`, which returns its
    // argument, so the address is used as it stands; one pointer argument.
    ExprPtr placement;
    std::vector<ExprPtr> userPlacement;
    bool nothrow = false;
    if (peek().is("(")) {
        const std::size_t ppos = peek().pos;
        at_++;
        if ([this] { std::size_t save = at_; bool t = atTypeName(); at_ = save; return t; }())
            src_.fail(ppos, "a parenthesised type after 'new' is read as a "
                            "placement here, so write 'new int' rather than "
                            "'new (int)'");
        placement = decay(assign());
        while (consume(",")) userPlacement.push_back(decay(assign()));
        expect(")");
        // **`new (std::nothrow) T`** - [support.dynamic]: the library's
        // nothrow allocator, which answers null where the plain one throws.
        const Type *pt = placement->type()->unqualified();
        nothrow = userPlacement.empty() && pt->isStructOrUnion() &&
                  pt->tag() == "std::nothrow_t";
        // **Anything else reaches a user-declared `operator new`** - [expr.new]/14:
        // a second argument, or a first that is not a pointer, is a placement
        // argument the class's or the global allocation functions are asked to take.
        if (!nothrow && (!userPlacement.empty() || !pt->isPointer())) {
            userPlacement.insert(userPlacement.begin(), std::move(placement));
            placement = nullptr;
        }
        (void)ppos;
    }
    const bool userAlloc = !userPlacement.empty();

    StorageClass sc = StorageNone;
    const Type *made = specifiers(&sc);
    if (sc != StorageNone)
        src_.fail(pos, "a storage class has no meaning in a new-expression");

    // The pointer part of the new-type-id, by hand: `declarator` reads an array
    // bound with constantExpression, and the whole point of `new T[n]` is that
    // n need not be one.
    for (;;) {
        if (consume("*")) {
            made = types_.pointerTo(made);
            while (peek().is("const")) { at_++; made = types_.withConst(made); }
            continue;
        }
        break;
    }

    ExprPtr count;
    bool array = false;
    if (consume("[")) {
        array = true;
        count = expr();
        expect("]");
        if (peek().is("["))
            src_.fail(peek().pos, "an array of arrays from 'new' is not "
                                  "supported yet - only the first dimension "
                                  "may be given here");
    }

    if (!made->isComplete())
        src_.fail(pos, "'new' needs a complete type, and '" + made->describe() +
                       "' is not one here");
    if (made->isReference())
        src_.fail(pos, "'new' cannot make a reference - a reference is a name "
                       "for something that already exists");

    // The initialiser, and only the forms that need no constructor; anything else
    // is refused by name rather than half-built. A class with constructors is
    // built by calling one, here as much as on the stack.
    checkNotAbstract(made, pos, "the object 'new' would make");
    const bool constructed = made->isStructOrUnion() && !made->tag().empty() &&
                             overloadsOf(constructorKey(made->tag())) != nullptr;
    std::vector<ExprPtr> ctorArgs;
    bool hasInit = false;
    ExprPtr init;
    // **`new T{}` is `new T()`.** [dcl.init]/11 sends both to value-initialisation,
    // and the empty pair is the only braces read here: every branch below takes
    // "nothing inside the parentheses" to mean exactly that.
    const bool braces = peek().is("{");
    if (braces && !peekAt(1).is("}"))
        src_.fail(peek().pos, "'new " + made->describe() + "{...}' is "
                              "list-initialisation, and that is not supported "
                              "yet - write 'new " + made->describe() +
                              "(...)'. The empty pair, 'new " +
                              made->describe() + "{}', is read: it "
                              "value-initialises");
    if (braces) {
        at_ += 2;
        hasInit = true;
    } else if (peek().is("(")) {
        at_++;
        hasInit = true;
        if (array) {
            // **`new T[n]()` value-initialises every element** - [expr.new]/17
            // allows exactly the empty pair there and nothing inside it, so
            // `new int[n](5)` is refused the way clang refuses it.
            if (!consume(")"))
                src_.fail(peek().pos, "'new T[n](x)' cannot initialise an "
                                      "array - only the empty '()' is allowed "
                                      "there, and it zeroes every element");
        } else if (constructed) {
            if (!peek().is(")")) parseArguments(ctorArgs);
            else at_++;
        } else if (!consume(")")) {
            init = assign();
            if (peek().is(","))
                src_.fail(peek().pos, "more than one value in a new-expression "
                                      "needs a constructor, which is not "
                                      "supported yet");
            expect(")");
        }
    }
    const Type *sizeT = types_.get(target_.sizeType());
    ExprPtr bytes(new Num(static_cast<long long>(made->size(target_))));
    bytes->setType(sizeT);
    // **An array of a class**: the count is wanted by the cookie, the
    // constructor loop and the allocator, so it goes into a slot. [expr.new]/12:
    // the reserved placement form adds no cookie - measured on both ABIs.
    const int cookie = array && (placement == nullptr || nothrow)
                     ? arrayCookie(made) : 0;
    int countSlot = 0;
    std::string countTemp;
    if (array) {
        ExprPtr n = convert(decay(std::move(count)), sizeT);
        if (constructed || cookie != 0) {
            countSlot = allocateFrameSlot(sizeT);
            countTemp = ".newc" + std::to_string(newTemps_++);
            ExprPtr held(Var::local(countTemp, countSlot));
            held->setType(sizeT);
            ExprPtr save(new Assign(std::move(held), std::move(n)));
            save->setType(sizeT);
            n = std::move(save);
        }
        ExprPtr total(new Binary(BinOp::Mul, std::move(n), std::move(bytes)));
        total->setType(sizeT);
        bytes = std::move(total);
        if (cookie != 0) {
            ExprPtr extra(new Num(static_cast<long long>(cookie)));
            extra->setType(sizeT);
            ExprPtr sum(new Binary(BinOp::Add, std::move(bytes), std::move(extra)));
            sum->setType(sizeT);
            bytes = std::move(sum);
        }
    }
    // The byte count is wanted twice for `new T[n]()` - once by the allocator
    // and once by the zeroing - and n is any expression, so it is computed
    // once into a slot and the allocator reads the assignment's value.
    int bytesSlot = 0;
    std::string bytesTemp;
    if (array && hasInit) {
        bytesSlot = allocateFrameSlot(sizeT);
        bytesTemp = ".newn" + std::to_string(newTemps_);
        ExprPtr held(Var::local(bytesTemp, bytesSlot));
        held->setType(sizeT);
        ExprPtr save(new Assign(std::move(held), std::move(bytes)));
        save->setType(sizeT);
        bytes = std::move(save);
    }

    const Type *pointer = types_.pointerTo(made);
    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    ExprPtr raw;
    if (nothrow) {
        raw = callNothrowAllocator(array, std::move(bytes), std::move(placement), pos);
    } else if (placement != nullptr) {
        raw.reset(new Cast(voidPtr, std::move(placement)));
        raw->setType(voidPtr);
        // The count's slot is written inside `bytes`, which nothing allocates with here.
        if (array) {
            ExprPtr counted(new Comma(std::move(bytes), std::move(raw)));
            counted->setType(voidPtr);
            raw = std::move(counted);
        }
    } else if (userAlloc) {
        raw = userPlacementAllocation(made, array, std::move(bytes), userPlacement, pos);
    } else if (const Signature *own = classAllocator(made, array ? "operatornew[]" : "operatornew")) {
        // [class.free]/2: a class's own `operator new` allocates its objects.
        markUsed(own);
        std::vector<ExprPtr> args;
        args.push_back(std::move(bytes));
        raw = completeCall(own->name, own->symbol, nullptr, own->returns,
                           own->params, false, pos, std::move(args));
    } else {
        // operator new takes a size_t, and its name says which type that is:
        // `m` on the LP64 targets, `y` on Windows, `j` on the C6000 (_Znwj).
        const std::string alloc = std::string(array ? "_Zna" : "_Znw") +
                                  itaniumBuiltinCode(target_.sizeType());
        raw = callAllocator(alloc.c_str(),
                            array ? "??_U@YAPEAX_K@Z" : "??2@YAPEAX_K@Z",
                            voidPtr, std::move(bytes), pos);
    }
    // The cookie: the allocation held, the count written into its last
    // size_t, and the array beginning after it.
    if (cookie != 0) {
        const Type *chars = types_.pointerTo(types_.get(Kind::Char));
        const int rawSlot = allocateFrameSlot(voidPtr);
        const std::string rawTemp = ".newr" + std::to_string(newTemps_++);
        auto rawVar = [&]() { ExprPtr e(Var::local(rawTemp, rawSlot)); e->setType(voidPtr); return e; };
        ExprPtr keep(new Assign(rawVar(), std::move(raw)));
        keep->setType(voidPtr);
        ExprPtr asChars(new Cast(chars, rawVar()));
        asChars->setType(chars);
        ExprPtr countAt(new Num(static_cast<long long>(cookie - sizeT->size(target_))));
        countAt->setType(types_.intType());
        ExprPtr countAddr(new Binary(BinOp::Add, std::move(asChars), std::move(countAt)));
        countAddr->setType(chars);
        ExprPtr countPtr(new Cast(types_.pointerTo(sizeT), std::move(countAddr)));
        countPtr->setType(types_.pointerTo(sizeT));
        ExprPtr countCell(new Unary('*', std::move(countPtr)));
        countCell->setType(sizeT);
        ExprPtr countVal(Var::local(countTemp, countSlot));
        countVal->setType(sizeT);
        ExprPtr write(new Assign(std::move(countCell), std::move(countVal)));
        write->setType(sizeT);
        // The ARM cookie's first word is the element size, in front of the count.
        if (target_.armArrayCookie()) {
            ExprPtr asChars3(new Cast(chars, rawVar()));
            asChars3->setType(chars);
            ExprPtr sizeAt(new Num(static_cast<long long>(cookie - 2 * sizeT->size(target_))));
            sizeAt->setType(types_.intType());
            ExprPtr sizeAddr(new Binary(BinOp::Add, std::move(asChars3), std::move(sizeAt)));
            sizeAddr->setType(chars);
            ExprPtr sizePtr(new Cast(types_.pointerTo(sizeT), std::move(sizeAddr)));
            sizePtr->setType(types_.pointerTo(sizeT));
            ExprPtr sizeCell(new Unary('*', std::move(sizePtr)));
            sizeCell->setType(sizeT);
            ExprPtr elemSize(new Num(static_cast<long long>(made->size(target_))));
            elemSize->setType(sizeT);
            ExprPtr writeSize(new Assign(std::move(sizeCell), std::move(elemSize)));
            writeSize->setType(sizeT);
            ExprPtr both(new Comma(std::move(writeSize), std::move(write)));
            both->setType(sizeT);
            write = std::move(both);
        }
        ExprPtr asChars2(new Cast(chars, rawVar()));
        asChars2->setType(chars);
        ExprPtr skip(new Num(static_cast<long long>(cookie)));
        skip->setType(types_.intType());
        ExprPtr start(new Binary(BinOp::Add, std::move(asChars2), std::move(skip)));
        start->setType(chars);
        ExprPtr seq1(new Comma(std::move(keep), std::move(write)));
        seq1->setType(sizeT);
        ExprPtr seq2(new Comma(std::move(seq1), std::move(start)));
        seq2->setType(chars);
        raw.reset(new Cast(voidPtr, std::move(seq2)));
        raw->setType(voidPtr);
    }
    ExprPtr typed(new Cast(pointer, std::move(raw)));
    typed->setType(pointer);

    // **`new T[n]` of a class runs the default constructor once per element**
    // - [expr.new]/17 - by the class's loop, given the array and the count.
    if (constructed && array) {
        const int baseSlot = allocateFrameSlot(pointer);
        const std::string baseTemp = ".newb" + std::to_string(newTemps_++);
        ExprPtr held(Var::local(baseTemp, baseSlot));
        held->setType(pointer);
        ExprPtr keep(new Assign(std::move(held), std::move(typed)));
        keep->setType(pointer);
        ExprPtr again(Var::local(baseTemp, baseSlot));
        again->setType(pointer);
        ExprPtr n(Var::local(countTemp, countSlot));
        n->setType(sizeT);
        ExprPtr built = callVectorLoop(vectorConstructor(made, pos), made,
                                       std::move(again), std::move(n), pos);
        // [expr.new]/13: a null from the nothrow allocator initialises nothing.
        if (nothrow) built = guardAgainstNull(baseTemp, baseSlot, pointer, std::move(built));
        // [expr.new]/20: an element constructor that throws gives the storage back.
        if ((placement == nullptr && !userAlloc) || nothrow) {
            const int g = guardFlag();
            Temporary t{ baseSlot, made, g };
            t.newStorage = true; t.newArray = true; t.newNothrow = nothrow; t.newCookie = cookie;
            statementTemps_.push_back(t);
            ExprPtr armed(new Comma(std::move(keep), setGuard(g, 1)));
            armed->setType(types_.intType());
            keep = std::move(armed);
            ExprPtr done(new Comma(std::move(built), setGuard(g, 0)));
            done->setType(types_.intType());
            built = std::move(done);
        }
        ExprPtr seq(new Comma(std::move(keep), std::move(built)));
        seq->setType(types_.get(Kind::Void));
        ExprPtr result(Var::local(baseTemp, baseSlot));
        result->setType(pointer);
        ExprPtr whole(new Comma(std::move(seq), std::move(result)));
        whole->setType(pointer);
        return whole;
    }

    if (!hasInit && !constructed) return typed;

    // `new int(5)` is an allocation and a store where an expression yields one
    // value, so the pointer is kept in a temporary and the comma sequences them,
    // as bindReference does. A constructed object puts a call where the store is.
    int slot = allocateFrameSlot(pointer);
    std::string temp = ".new" + std::to_string(newTemps_++);

    ExprPtr held(Var::local(temp, slot));
    held->setType(pointer);
    ExprPtr keep(new Assign(std::move(held), std::move(typed)));
    keep->setType(pointer);

    if (array) {
        // **n elements zeroed by the platform's `memset`**, as the storage came
        // from its `operator new[]`: n is a run-time value and this expression
        // language has no loop. It is also what clang emits for the same line.
        const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
        std::vector<ExprPtr> args;
        ExprPtr at(Var::local(temp, slot));
        at->setType(pointer);
        ExprPtr asVoid(new Cast(voidPtr, std::move(at)));
        asVoid->setType(voidPtr);
        args.push_back(std::move(asVoid));
        ExprPtr zero(new Num(0LL));
        zero->setType(types_.intType());
        args.push_back(std::move(zero));
        ExprPtr n(Var::local(bytesTemp, bytesSlot));
        n->setType(sizeT);
        args.push_back(std::move(n));
        std::vector<int> argSlots(args.size(), 0);
        Call *fill = new Call("memset", nullptr, std::move(args), false, 0, -1,
                              std::move(argSlots));
        fill->setSymbol("memset");
        ExprPtr filled(fill);
        filled->setType(voidPtr);
        if (nothrow) filled = guardAgainstNull(temp, slot, pointer, std::move(filled));

        ExprPtr result(Var::local(temp, slot));
        result->setType(pointer);
        ExprPtr both(new Comma(std::move(keep), std::move(filled)));
        both->setType(voidPtr);
        ExprPtr all(new Comma(std::move(both), std::move(result)));
        all->setType(pointer);
        return all;
    }

    if (constructed) {
        convertThroughConversionFunction(ctorArgs, made, true, pos);
        const Signature &ctor = resolveOverload(constructorKey(made->tag()),
                                                ctorArgs, pos);
        // **[class.access]/1 applies to a constructor a new-expression
        // calls**, as it does to every other way of building one.
        if (!accessibleFrom(currentClass_, made, ctor.access) &&
            !isFriendOf(made))
            src_.fail(pos, "'" + made->describe() + "' is built here and the "
                           "constructor that matches is " +
                           (ctor.access == Access::Private ? "private"
                                                           : "protected"));
        // `new V()` and not `new V`, for a class whose default constructor nobody wrote.
        const bool zeroFirst = hasInit && ctorArgs.empty() && ctor.implicit;
        // **The defaults, as every other constructor call reads them.** This call
        // is built by hand, one argument per parameter, and `new M` of an
        // `M(int a = 5)` was refused after resolution had already accepted it.
        applyDefaults(ctor, ctorArgs, pos);
        std::vector<ExprPtr> all;
        ExprPtr self(Var::local(temp, slot));
        self->setType(pointer);
        all.push_back(std::move(self));
        for (std::size_t i = 0; i < ctorArgs.size(); i++)
            all.push_back(std::move(ctorArgs[i]));

        std::vector<const Type *> full;
        full.push_back(pointer);
        for (std::size_t i = 0; i < ctor.params.size(); i++)
            full.push_back(ctor.params[i]);

        ExprPtr build = completeCall(made->tag(), ctor.symbol, nullptr,
                                     types_.get(Kind::Void), full, false, pos,
                                     std::move(all));
        if (zeroFirst) {
            ExprPtr p(Var::local(temp, slot));
            p->setType(pointer);
            ExprPtr obj(new Unary('*', std::move(p)));
            obj->setType(made);
            if (ExprPtr chain = zeroChain(*obj, made)) {
                ExprPtr seq(new Comma(std::move(chain), std::move(build)));
                seq->setType(types_.get(Kind::Void));
                build = std::move(seq);
            }
        }
        if (nothrow) build = guardAgainstNull(temp, slot, pointer, std::move(build));
        // [expr.new]/20: a constructor that throws gives the storage back.
        if (!ctor.isNoexcept && ((placement == nullptr && !userAlloc) || nothrow)) {
            const int g = guardFlag();
            Temporary t{ slot, made, g };
            t.newStorage = true; t.newNothrow = nothrow;
            statementTemps_.push_back(t);
            ExprPtr armed(new Comma(std::move(keep), setGuard(g, 1)));
            armed->setType(types_.intType());
            keep = std::move(armed);
            ExprPtr done(new Comma(std::move(build), setGuard(g, 0)));
            done->setType(types_.intType());
            build = std::move(done);
        }
        ExprPtr made2(new Comma(std::move(keep), std::move(build)));
        made2->setType(types_.get(Kind::Void));

        ExprPtr answer(Var::local(temp, slot));
        answer->setType(pointer);
        ExprPtr whole(new Comma(std::move(made2), std::move(answer)));
        whole->setType(pointer);
        return whole;
    }

    ExprPtr base(Var::local(temp, slot));
    base->setType(pointer);
    ExprPtr where(new Unary('*', std::move(base)));
    where->setType(made);

    // **`new P()` for a class with no constructor is value-initialisation too**,
    // which [dcl.init]/8 makes a zeroing. The scalar branch converted its 0 to the
    // class type, so `new P()` read address 0; the stores go through the pointer.
    if (!init && made->isStructOrUnion()) {
        ExprPtr result0(Var::local(temp, slot));
        result0->setType(pointer);
        ExprPtr chain = zeroChain(*where, made);
        if (chain == nullptr) {
            // An empty class: nothing observable to set, the allocation is
            // the whole of the work.
            ExprPtr all(new Comma(std::move(keep), std::move(result0)));
            all->setType(pointer);
            return all;
        }
        if (nothrow) chain = guardAgainstNull(temp, slot, pointer, std::move(chain));
        const Type *ct = chain->type();
        ExprPtr both(new Comma(std::move(keep), std::move(chain)));
        both->setType(ct);
        ExprPtr all(new Comma(std::move(both), std::move(result0)));
        all->setType(pointer);
        return all;
    }

    // `new int()` is value-initialisation, which for these types is a zero.
    ExprPtr value;
    if (init) {
        checkAssignable(*init, made, pos, "the value in a new-expression");
        value = convert(decay(std::move(init)), made);
    } else {
        value.reset(new Num(0LL));
        value->setType(types_.intType());
        value = convert(std::move(value), made);
    }
    ExprPtr store(new Assign(std::move(where), std::move(value)));
    store->setType(made);
    if (nothrow) store = guardAgainstNull(temp, slot, pointer, std::move(store));
    const Type *storeType = store->type();

    ExprPtr result(Var::local(temp, slot));
    result->setType(pointer);

    ExprPtr both(new Comma(std::move(keep), std::move(store)));
    both->setType(storeType);
    ExprPtr all(new Comma(std::move(both), std::move(result)));
    all->setType(pointer);
    return all;
}

ExprPtr Parser::deleteExpression(std::size_t pos) {
    bool array = false;
    if (consume("[")) { expect("]"); array = true; }

    ExprPtr what = decay(unary());
    const Type *t = what->type();
    if (!t->isPointer())
        src_.fail(pos, "'delete' needs a pointer, and this is '" +
                       t->describe() + "'");
    if (t->pointee()->isVoid())
        src_.fail(pos, "'delete' of a 'void *' does not know what it is "
                       "freeing - give it the pointer's real type");

    // **The destructor runs before the memory goes back**, as clang emits it.
    const Signature *dtor = destructorOf(t->pointee());

    // **A virtual destructor is reached through the vtable**, the static type
    // not being the one to destroy; the slot's deleting form frees as well.
    // `delete[]` takes the static type - [expr.delete]/3 - and goes below.
    if (dtor != nullptr && dtor->isVirtual && !array) {
        const Type *cls = t->pointee()->unqualified();
        int slot = allocateFrameSlot(t);
        std::string temp = ".dv" + std::to_string(refTemps_++);
        ExprPtr keep(Var::local(temp, slot));
        keep->setType(t);
        ExprPtr save(new Assign(std::move(keep), std::move(what)));
        save->setType(t);
        ExprPtr call = virtualDestructorCall(temp, slot, t, cls, true, pos);
        ExprPtr both(new Comma(std::move(save),
                               guardAgainstNull(temp, slot, t, std::move(call))));
        both->setType(types_.get(Kind::Void));
        return both;
    }

    // **`delete[] p` of a class with a destructor**: the count from the
    // cookie in front of the array, the elements destroyed last first by
    // the class's loop, and the allocation freed from where it began.
    if (dtor != nullptr && array) {
        const Type *sizeT = types_.get(target_.sizeType());
        const Type *chars = types_.pointerTo(types_.get(Kind::Char));
        const int cookie = arrayCookie(t->pointee());
        const int slot = allocateFrameSlot(t);
        const std::string temp = ".del" + std::to_string(refTemps_++);
        auto held = [&]() { ExprPtr e(Var::local(temp, slot)); e->setType(t); return e; };
        ExprPtr save(new Assign(held(), std::move(what)));
        save->setType(t);

        ExprPtr asChars(new Cast(chars, held()));
        asChars->setType(chars);
        ExprPtr back(new Num(static_cast<long long>(-sizeT->size(target_))));
        back->setType(types_.intType());
        ExprPtr countAddr(new Binary(BinOp::Add, std::move(asChars), std::move(back)));
        countAddr->setType(chars);
        ExprPtr countPtr(new Cast(types_.pointerTo(sizeT), std::move(countAddr)));
        countPtr->setType(types_.pointerTo(sizeT));
        ExprPtr n(new Unary('*', std::move(countPtr)));
        n->setType(sizeT);
        ExprPtr run = callVectorLoop(vectorDestructor(t->pointee()->unqualified(), pos),
                                     t->pointee()->unqualified(), held(), std::move(n), pos);

        ExprPtr asChars2(new Cast(chars, held()));
        asChars2->setType(chars);
        ExprPtr toStart(new Num(static_cast<long long>(-cookie)));
        toStart->setType(types_.intType());
        ExprPtr start(new Binary(BinOp::Add, std::move(asChars2), std::move(toStart)));
        start->setType(chars);
        const Type *vp = types_.pointerTo(types_.get(Kind::Void));
        ExprPtr freed(new Cast(vp, std::move(start)));
        freed->setType(vp);
        ExprPtr release = deallocateArray(t->pointee(), std::move(freed), pos);
        ExprPtr both(new Comma(std::move(run), std::move(release)));
        both->setType(types_.get(Kind::Void));
        ExprPtr all(new Comma(std::move(save),
                              guardAgainstNull(temp, slot, t, std::move(both))));
        all->setType(types_.get(Kind::Void));
        return all;
    }

    if (dtor != nullptr) {
        int slot = allocateFrameSlot(t);
        std::string temp = ".del" + std::to_string(refTemps_++);

        ExprPtr keep(Var::local(temp, slot));
        keep->setType(t);
        ExprPtr save(new Assign(std::move(keep), std::move(what)));
        save->setType(t);

        ExprPtr held(Var::local(temp, slot));
        held->setType(t);
        ExprPtr run = destructorCall(std::move(held), *dtor, pos);

        ExprPtr both(new Comma(std::move(save),
                               guardAgainstNull(temp, slot, t, std::move(run))));
        both->setType(types_.get(Kind::Void));

        ExprPtr again(Var::local(temp, slot));
        again->setType(t);
        const Type *vp = types_.pointerTo(types_.get(Kind::Void));
        ExprPtr freed(new Cast(vp, std::move(again)));
        freed->setType(vp);
        ExprPtr release = deallocate(t->pointee(), std::move(freed), pos);
        ExprPtr all(new Comma(std::move(both), std::move(release)));
        all->setType(types_.get(Kind::Void));
        return all;
    }

    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    ExprPtr raw(new Cast(voidPtr, std::move(what)));
    raw->setType(voidPtr);

    if (array) return deallocateArray(t->pointee(), std::move(raw), pos);
    return deallocate(t->pointee(), std::move(raw), pos);
}

// **The deleting slot frees; the complete-object slot does not.** Itanium
// keeps the two side by side, D1 then D0; Microsoft keeps one `??_G` and a
// flag whose low bit says whether to free - measured from cl, and 0 is safe.
ExprPtr Parser::virtualDestructorCall(const std::string &temp, int slot,
                                      const Type *t, const Type *cls,
                                      bool deleting, std::size_t pos) {
    const bool ms = target_.microsoftNames();
    const std::vector<VSlot> &slots = vtables_[cls->tag()];
    int index = -1;
    const char *want = ms || !deleting ? "~" : "~$deleting";
    for (std::size_t i = 0; i < slots.size(); i++)
        if (slots[i].name == want) { index = static_cast<int>(i); break; }
    if (index < 0)
        src_.fail(pos, "'" + cls->describe() + "' has a virtual destructor "
                       "with no slot for it");

    std::vector<const Type *> full;
    full.push_back(t);
    const Type *flagType = types_.get(Kind::UInt);
    if (ms) full.push_back(flagType);
    const Type *ret = ms ? types_.pointerTo(types_.get(Kind::Void))
                         : types_.get(Kind::Void);
    const Type *fnType = types_.functionType(ret, full, false);
    const Type *fnPtr = types_.pointerTo(fnType);
    const Type *table = types_.pointerTo(fnPtr);

    ExprPtr load(Var::local(temp, slot));
    load->setType(t);
    ExprPtr asTable(new Cast(types_.pointerTo(table), std::move(load)));
    asTable->setType(types_.pointerTo(table));
    ExprPtr vptr(new Unary('*', std::move(asTable)));
    vptr->setType(table);
    if (index != 0) {
        // Bytes, not entries: a hand-built Add is not scaled by the pointee.
        ExprPtr at(new Num(static_cast<long long>(index) * fnPtr->size(target_)));
        at->setType(types_.intType());
        ExprPtr moved(new Binary(BinOp::Add, std::move(vptr), std::move(at)));
        moved->setType(table);
        vptr = std::move(moved);
    }
    ExprPtr entry(new Unary('*', std::move(vptr)));
    entry->setType(fnPtr);

    std::vector<ExprPtr> args;
    ExprPtr self(Var::local(temp, slot));
    self->setType(t);
    args.push_back(std::move(self));
    if (ms) {
        ExprPtr flag(new Num(deleting ? 1LL : 0LL));
        flag->setType(flagType);
        args.push_back(std::move(flag));
    }
    return completeCall("~", std::string(), std::move(entry), ret, full, false,
                        pos, std::move(args));
}

// A class's own `operator new` or `operator delete`, else null - looked up
// in the class and then its bases, as [class.free] finds them.
const Parser::Signature *Parser::classAllocator(const Type *made,
                                                const char *which) {
    const Type *cls = made->unqualified();
    while (cls != nullptr && cls->isStructOrUnion() && !cls->tag().empty()) {
        if (const std::vector<std::size_t> *set = overloadsOf(cls->tag() + "::" + which))
            return &functions_[(*set)[0]];
        const std::vector<Type::BaseSpec> &bs = cls->bases();
        cls = bs.empty() ? nullptr : bs[0].type->unqualified();
    }
    return nullptr;
}

// What `delete p` frees with: the class's own `operator delete` where it
// declared one, else the platform's.
ExprPtr Parser::deallocate(const Type *pointee, ExprPtr raw, std::size_t pos) {
    if (const Signature *own = classAllocator(pointee, "operatordelete")) {
        markUsed(own);
        std::vector<ExprPtr> args;
        args.push_back(std::move(raw));
        return completeCall(own->name, own->symbol, nullptr, own->returns,
                            own->params, false, pos, std::move(args));
    }
    return callAllocator("_ZdlPv", "??3@YAXPEAX@Z", types_.get(Kind::Void),
                         std::move(raw), pos);
}

// What `delete[] p` frees with, by the same rule - [class.free]/2 for the array form.
ExprPtr Parser::deallocateArray(const Type *pointee, ExprPtr raw, std::size_t pos) {
    if (const Signature *own = classAllocator(pointee, "operatordelete[]")) {
        markUsed(own);
        std::vector<ExprPtr> args;
        args.push_back(std::move(raw));
        return completeCall(own->name, own->symbol, nullptr, own->returns,
                            own->params, false, pos, std::move(args));
    }
    return callAllocator("_ZdaPv", "??_V@YAXPEAX@Z", types_.get(Kind::Void),
                         std::move(raw), pos);
}

// **`new (a, b) T` asks for `operator new(size_t, A, B)`** - [expr.new]/14, and
// [class.free]/2 asks the class first: a class that declares any `operator new`
// hides the global ones, as clang refuses `new (1, 2.0) S` for such an S.
ExprPtr Parser::userPlacementAllocation(const Type *made, bool array, ExprPtr bytes,
                                        std::vector<ExprPtr> &extra, std::size_t pos) {
    const char *which = array ? "operatornew[]" : "operatornew";
    std::string key = which;
    for (const Type *cls = made->unqualified();
         cls != nullptr && cls->isStructOrUnion() && !cls->tag().empty();
         cls = cls->bases().empty() ? nullptr : cls->bases()[0].type->unqualified())
        if (overloadsOf(cls->tag() + "::" + which) != nullptr) { key = cls->tag() + "::" + which; break; }
    if (overloadsOf(key) == nullptr)
        src_.fail(pos, "'new' with placement arguments needs an 'operator " +
                       std::string(array ? "new[]" : "new") + "' declared to "
                       "take them, and none is declared");
    std::vector<ExprPtr> args;
    args.push_back(std::move(bytes));
    for (std::size_t i = 0; i < extra.size(); i++) args.push_back(std::move(extra[i]));
    const Signature sig = resolveOverload(key, args, pos);
    for (std::size_t i = 1; i < args.size() && i < sig.params.size(); i++)
        if (!sig.params[i]->isReference()) args[i] = convert(std::move(args[i]), sig.params[i]);
    return completeCall(sig.name, sig.symbol, nullptr, sig.returns, sig.params,
                        false, pos, std::move(args));
}

// `operator delete(void *, const std::nothrow_t &)` and its array twin, for the
// storage a nothrow allocation made - [expr.new]/20 pairs it with the allocator.
ExprPtr Parser::callNothrowDeallocator(bool array, ExprPtr raw) {
    const char *itanium = array ? "_ZdaPvRKSt9nothrow_t" : "_ZdlPvRKSt9nothrow_t";
    const char *microsoft = array ? "??_V@YAXPEAXAEBUnothrow_t@std@@@Z"
                                  : "??3@YAXPEAXAEBUnothrow_t@std@@@Z";
    const char *tag = target_.microsoftNames() ? "?nothrow@std@@3Unothrow_t@1@B" : "_ZSt7nothrow";
    Var *tv = Var::global(tag);
    tv->setSymbol(tag);
    ExprPtr tref(tv);
    tref->setType(types_.get(Kind::Char));
    ExprPtr taddr(new Unary('&', std::move(tref)));
    taddr->setType(types_.pointerTo(types_.get(Kind::Char)));
    std::vector<ExprPtr> args;
    args.push_back(std::move(raw));
    args.push_back(std::move(taddr));
    std::vector<int> argSlots(args.size(), 0);
    const std::string name = target_.microsoftNames() ? microsoft : itanium;
    Call *call = new Call(name, nullptr, std::move(args), false, 0, 2, std::move(argSlots));
    call->setSymbol(name);
    ExprPtr n(call);
    n->setType(types_.get(Kind::Void));
    return n;
}

// **[expr.delete]/2: deleting a null pointer has no effect**, and running the
// destructor on one is how `delete p;` crashed. Written `p != 0 ? (call, 1) : 0`
// rather than with void arms, an int each side being a shape every backend emits.
ExprPtr Parser::guardAgainstNull(const std::string &temp, int slot,
                                 const Type *ptr, ExprPtr body) {
    ExprPtr probe(Var::local(temp, slot));
    probe->setType(ptr);
    ExprPtr n(new Num(static_cast<long long>(0)));
    n->setType(types_.intType());
    ExprPtr test(new Binary(BinOp::Ne, std::move(probe), convert(std::move(n), ptr)));
    test->setType(types_.intType());

    ExprPtr one(new Num(static_cast<long long>(1)));
    one->setType(types_.intType());
    ExprPtr ran(new Comma(std::move(body), std::move(one)));
    ran->setType(types_.intType());

    ExprPtr skipped(new Num(static_cast<long long>(0)));
    skipped->setType(types_.intType());

    ExprPtr guarded(new Conditional(std::move(test), std::move(ran),
                                    std::move(skipped)));
    guarded->setType(types_.intType());
    return guarded;
}

// **`typeid` names a type_info object** - [expr.typeid]: the static type's
// for a type-id or a non-polymorphic operand, unevaluated; read through the
// vptr for a polymorphic glvalue, a word before the vtable's address point.
ExprPtr Parser::typeidExpression(std::size_t pos) {
    if (target_.microsoftNames())
        src_.fail(pos, "'typeid' is not supported yet for x86_64-windows - the "
                       "Microsoft ABI answers it with a type descriptor per "
                       "type and __RTtypeid, which are not built here");
    const Type *info = findTypedef("std::type_info");
    if (info == nullptr || !info->isStructOrUnion())
        src_.fail(pos, "'typeid' needs std::type_info, so include <typeinfo>");
    const Type *infoPtr = types_.pointerTo(types_.withConst(info));
    expect("(");

    const Type *named = nullptr;
    ExprPtr operand;
    // The temporaries an operand makes are kept only when it is evaluated -
    // the polymorphic glvalue below; the static form is unevaluated and
    // hands them back the way sizeof does.
    const std::vector<Temporary> savedTemps = pendingTemps_;
    const std::vector<Alive> savedAlive = alive_;
    if ([this] { std::size_t save = at_; bool t = atTypeName(); at_ = save; return t; }()) {
        StorageClass sc;
        named = declarator(specifiers(&sc), true).type;
    } else {
        operand = expr();
    }
    expect(")");

    ExprPtr address;
    const Type *subject = named != nullptr ? named : operand->type();
    if (subject->isReference()) subject = subject->referent();
    subject = subject->unqualified();
    const bool glvalue = operand != nullptr &&
        (operand->type()->isReference() ||
         dynamic_cast<const MemberAccess *>(operand.get()) != nullptr ||
         (dynamic_cast<const Unary *>(operand.get()) != nullptr &&
          static_cast<const Unary *>(operand.get())->op() == '*') ||
         (dynamic_cast<const Var *>(operand.get()) != nullptr &&
          !static_cast<const Var *>(operand.get())->noAddress()));
    if (!(glvalue && subject->isStructOrUnion() && subject->polymorphic())) {
        pendingTemps_ = savedTemps;
        alive_ = savedAlive;
    }
    if (glvalue && subject->isStructOrUnion() && subject->polymorphic()) {
        // The object's first word is the vptr; the type_info pointer sits
        // one word below the address point it holds.
        const Type *chars = types_.pointerTo(types_.get(Kind::Char));
        ExprPtr at(new Unary('&', std::move(operand)));
        at->setType(types_.pointerTo(subject));
        ExprPtr vptrAt(new Cast(types_.pointerTo(chars), std::move(at)));
        vptrAt->setType(types_.pointerTo(chars));
        ExprPtr vptr(new Unary('*', std::move(vptrAt)));
        vptr->setType(chars);
        ExprPtr back(new Num(static_cast<long long>(-pointerBytes())));
        back->setType(types_.intType());
        ExprPtr slot(new Binary(BinOp::Add, std::move(vptr), std::move(back)));
        slot->setType(chars);
        ExprPtr slotPtr(new Cast(types_.pointerTo(infoPtr), std::move(slot)));
        slotPtr->setType(types_.pointerTo(infoPtr));
        address.reset(new Unary('*', std::move(slotPtr)));
        address->setType(infoPtr);
    } else {
        std::string why;
        const std::string sym = typeInfoSymbolFor(subject, pos, &why);
        if (sym.empty())
            src_.fail(pos, "'typeid' cannot name the type of this: " + why);
        Var *ti = Var::global(sym);
        ti->setSymbol(sym);
        ExprPtr ref(ti);
        ref->setType(types_.get(Kind::Char));
        ExprPtr addr(new Unary('&', std::move(ref)));
        addr->setType(types_.pointerTo(types_.get(Kind::Char)));
        address.reset(new Cast(infoPtr, std::move(addr)));
        address->setType(infoPtr);
    }
    ExprPtr object(new Unary('*', std::move(address)));
    object->setType(types_.withConst(info));
    return object;
}
