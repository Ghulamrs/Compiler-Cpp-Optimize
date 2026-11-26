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
    // [dcl.init]/6: zero converted to the type, a scoped enumeration's too, which no implicit conversion reaches
    // (1.82: `Agg()` and `: a()` of an aggregate holding an `enum class` were refused).
    const bool scoped = type->unqualified()->isScopedEnumeration();
    ExprPtr store(new Assign(std::move(target), convert(std::move(z), type, scoped)));
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
            Init in = parseInitialiser();
            // The list alone, and one the initializer_list takes: otherwise the list is an argument like
            // any other - `D({1, 2}, alloc)`, `string({"ab"})`.
            if (peek().is(")") && initializerListFits(ilType, in)) {
                args.push_back(initializerListExpr(ilType, in, pos));
                expect(")");
            } else {
                args.push_back(bracedPlaceholder(std::move(in)));
                if (consume(",")) parseArguments(args);
                else expect(")");
            }
            builtArgs = true;      // the ')' is consumed above
        }
    }
    if (!builtArgs) parseArguments(args);
    return classTemporaryFrom(cls, std::move(args), pos);
}

// **[expr.static.cast]/4: `static_cast<T>(e)` is `T t(e);`** - so a cast of an object to its own class, or
// to a base of it, is a copy of it: a temporary the copy or move constructor makes, as `T(e)` is. It was
// the object itself (1.77): `static_cast<K>(k).set(7)` wrote into k, no copy constructor ran, and the
// base's member read through such a cast of a derived object was refused by the code generator. A prvalue
// of the class itself is left as it is - the copy [class.copy]/31 lets an implementation leave out.
ExprPtr Parser::castToOwnClass(ExprPtr &v, const Type *to, std::size_t pos) {
    if (to->isReference() || v->type() == nullptr) return nullptr;
    const Type *plain = to->unqualified();
    const Type *from = v->type()->unqualified();
    if (!plain->isStructOrUnion() || !from->isStructOrUnion()) return nullptr;
    const bool same = from == plain;
    if (same && !isGlvalue(*v)) return nullptr;
    // A base that is there and cannot be reached is refused where it was.
    struct Base {
        static bool of(const Type *derived, const Type *base, int depth) {
            if (depth > 64) return false;
            for (const Type::BaseSpec &b : derived->unqualified()->bases())
                if (b.type->unqualified() == base || of(b.type, base, depth + 1)) return true;
            return false;
        }
    };
    if (!same && accessibleBaseOffset(from, plain) < 0 && Base::of(from, plain, 0)) return nullptr;
    // **A cast from another class is `T t(e);` too** - T's constructors are the candidates, its copy
    // constructor through e's conversion function among them, and a constructor that takes e as it is wins
    // (1.80: the conversion function was asked first, so with `Dst(const Src &)` and `Src::operator Dst()`
    // `static_cast<Dst>(s)` and `(Dst)s` called the function where `Dst(s)` called the constructor).
    std::vector<ExprPtr> one;
    one.push_back(std::move(v));
    return classTemporaryFrom(plain, std::move(one), pos);
}

int Parser::freshTemporarySlot(const Expr &e, const Type *cls) const {
    if (!e.isXvalue() || e.declCategory() != Expr::Category::Prvalue) return 0;
    if (e.type() == nullptr || cls == nullptr || e.type()->unqualified() != cls->unqualified()) return 0;
    const Expr *at = &e;
    // `*(construction, &$tmp)`, the shape a constructed temporary has; or the slot itself.
    if (const Unary *u = dynamic_cast<const Unary *>(at)) {
        if (u->op() != '*') return 0;
        const Comma *c = dynamic_cast<const Comma *>(&u->operand());
        if (c == nullptr) return 0;
        const Unary *a = dynamic_cast<const Unary *>(&c->right());
        if (a == nullptr || a->op() != '&') return 0;
        at = &a->operand();
    }
    const Var *v = dynamic_cast<const Var *>(at);
    if (v == nullptr || !v->isLocal() || v->name() != "$tmp") return 0;
    return v->offset();
}

Call *Parser::temporaryConstruction(const Expr &e) const {
    const Unary *u = dynamic_cast<const Unary *>(&e);
    if (u == nullptr || u->op() != '*') return nullptr;
    const Comma *both = dynamic_cast<const Comma *>(&u->operand());
    if (both == nullptr) return nullptr;
    const Expr *left = &both->left();
    if (const Comma *marked = dynamic_cast<const Comma *>(left)) left = &marked->left();
    const Call *call = dynamic_cast<const Call *>(left);
    if (call == nullptr || call->args().empty()) return nullptr;
    const Unary *at = dynamic_cast<const Unary *>(call->args()[0].get());
    if (at == nullptr || at->op() != '&') return nullptr;
    const Var *v = dynamic_cast<const Var *>(&at->operand());
    if (v == nullptr || !v->isLocal() || v->name() != "$tmp") return nullptr;
    return const_cast<Call *>(call);
}

void Parser::adoptTemporary(int slot) {
    for (std::size_t i = 0; i < pendingTemps_.size(); i++)
        if (pendingTemps_[i].slot == slot) {
            pendingTemps_.erase(pendingTemps_.begin() + static_cast<std::ptrdiff_t>(i));
            return;
        }
}

ExprPtr Parser::classTemporaryFrom(const Type *cls, std::vector<ExprPtr> args, std::size_t pos) {
    const Type *plain = cls->unqualified();
    // a member class of a specialization, named here for the first time: defined now, its constructors with it
    if (!plain->isComplete()) realizeMemberClass(plain);
    // `K(K(5))`: the temporary a temporary of the class would be copied into is that temporary ([class.copy]/31).
    if (args.size() == 1 && freshTemporarySlot(*args[0], plain) != 0 &&
        (copyConstructorOf(plain) != nullptr || moveConstructorOf(plain) != nullptr))
        return std::move(args[0]);
    const std::string key = constructorKey(plain->tag());
    buildBracedArguments(key, args, pos);
    // A class that declares no constructor has its implicit copy constructor all the same, and `P(v)` reaches
    // it through v's conversion function to P ([over.match.copy]/1). Until 1.80 only a class with constructors
    // asked, and `P(v)` for one without copied v's bytes as if v were a P - a wrong object, silently.
    convertThroughConversionFunction(args, plain, true, pos);
    if (!hasConstructors(plain->tag()) && args.size() == 1 && args[0]->type() != nullptr) {
        const Type *from = args[0]->type()->unqualified();
        if (from != plain && !(from->isStructOrUnion() && accessibleBaseOffset(from, plain) >= 0))
            src_.fail(pos, "'" + plain->describe() + "' declares no constructor, so '" + plain->tag() +
                           "(...)' can only copy another one - and '" + from->describe() +
                           "' is not one and has no conversion function to it");
    }
    const bool trivialCopyable = copyConstructorOf(plain) == nullptr && moveConstructorOf(plain) == nullptr;
    // From an object of a class derived from it, `CVector<4>(quat)`, the copy is of the base subobject.
    if (args.size() == 1 && args[0]->type() != nullptr && trivialCopyable && isGlvalue(*args[0]) &&
        args[0]->type()->unqualified() != plain && args[0]->type()->unqualified()->isStructOrUnion() &&
        accessibleBaseOffset(args[0]->type(), plain) >= 0) {
        args[0] = baseSubobject(std::move(args[0]), plain);
    }
    // **`T(t)` for a class whose constructors leave its copy trivial** is a
    // copy of bytes too: no copy constructor was declared, so resolution had
    // nothing to find and `T b = T(T::make())` was refused.
    const bool trivialSameClass =
        args.size() == 1 && args[0]->type() != nullptr &&
        args[0]->type()->unqualified() == plain && trivialCopyable;
    if (!hasConstructors(plain->tag()) || trivialSameClass) {
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
        obj->setDeclCategory(Expr::Category::Prvalue);
        if (args.empty()) {
            // **[dcl.init]/8: `P()` value-initialises: zeroed when no
            // constructor is user-provided** (it read the frame's leftovers);
            // named `$vinit`, which a constant reads as zeroes.
            obj.reset(Var::local("$vinit", slot));
            obj->setType(plain);
            obj->setXvalue();
            obj->setDeclCategory(Expr::Category::Prvalue);
            ExprPtr chain = zeroChain(*obj, plain);
            if (chain == nullptr) return obj;
            // Comma'd with the object's *address* and dereferenced, which is
            // the shape the argument path below already uses: a comma whose
            // value is a class lvalue is not something every backend spells.
            ExprPtr again(Var::local("$vinit", slot));
            again->setType(plain);
            ExprPtr at(new Unary('&', std::move(again)));
            at->setType(types_.pointerTo(plain));
            ExprPtr both(new Comma(std::move(chain), std::move(at)));
            both->setType(types_.pointerTo(plain));
            ExprPtr made(new Unary('*', std::move(both)));
            made->setType(plain);
            made->setXvalue();
            made->setDeclCategory(Expr::Category::Prvalue);
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
        made->setDeclCategory(Expr::Category::Prvalue);
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

// The backing array and the list object are statements; an expression carries them
// as a comma chain in front of the list, the shape `Row({1, 2})` already used.
ExprPtr Parser::initializerListExpr(const Type *ilType, Init &in, std::size_t pos) {
    std::vector<StmtPtr> setup;
    ExprPtr list = buildInitializerList(ilType, in, pos, setup, true);
    ExprPtr chain;
    for (std::size_t i = 0; i < setup.size(); i++) {
        ExprStmt *one = dynamic_cast<ExprStmt *>(setup[i].get());
        if (one == nullptr)
            src_.fail(pos, "this braced list needs setup an expression cannot carry - "
                           "name a variable and initialise it with the braces instead");
        ExprPtr e = one->release();
        const Type *t = e->type();
        if (chain == nullptr) { chain = std::move(e); continue; }
        ExprPtr joined(new Comma(std::move(chain), std::move(e)));
        joined->setType(t);
        chain = std::move(joined);
    }
    if (chain == nullptr) return list;
    const Type *lt = list->type();
    ExprPtr seq(new Comma(std::move(chain), std::move(list)));
    seq->setType(lt);
    return seq;
}

bool Parser::isInitializerListType(const Type *t) {
    const Type *p = t->unqualified();
    return p->isStructOrUnion() && p->isSpecialization() && p->templateName() == "initializer_list" &&
           (p->templateNamespace() == "std" || p->templateNamespace() == "std::");
}

// The element type is a typedef no program can spell, so any type can be written as one token.
const Type *Parser::initializerListOf(const Type *elem, std::size_t pos) {
    std::map<std::string, TemplateDecl>::const_iterator it = findTemplate("std::initializer_list");
    if (it == templates_.end() || !it->second.isClass ||
        (it->second.ns != "std" && it->second.ns != "std::"))
        src_.fail(pos, "a braced list here makes a 'std::initializer_list', and none is declared - "
                       "include <initializer_list>");
    const std::string name = "$ilelem" + std::to_string(ilElemSeq_++);
    declareTypeName(name, elem);
    const std::size_t resume = at_;
    at_ = tokens_.size();
    Token t;
    t.pos = pos;
    t.kind = TokenKind::Punct; t.text = "<";   tokens_.push_back(t);
    t.kind = TokenKind::Ident; t.text = name;  tokens_.push_back(t);
    t.kind = TokenKind::Punct; t.text = ">";   tokens_.push_back(t);
    t.kind = TokenKind::End;   t.text = "";    tokens_.push_back(t);
    const TemplateDecl decl = it->second;
    const Type *made = instantiateClass(decl, pos);
    at_ = resume;
    return made;
}

// [dcl.spec.auto]/6 with [temp.deduct.call]/1: every element deduces E, and they must agree.
const Type *Parser::deduceBracedAuto(const std::string &name, std::size_t pos) {
    const Type *elem = nullptr;
    const std::size_t brace = peek().pos;
    {
        Discarded held(this);
        Init in = parseInitialiser();
        if (in.items.empty())
            src_.fail(pos, "'" + name + "' is declared 'auto' from '{}', which has no element to "
                           "deduce a type from");
        for (std::size_t i = 0; i < in.items.size(); i++) {
            if (in.items[i].isList)
                src_.fail(in.items[i].pos, "'" + name + "' is declared 'auto' from a list holding "
                                           "a braced list, which has no type to deduce");
            const Type *e = decayedType(in.items[i].value->type());
            if (elem != nullptr && e != elem)
                src_.fail(brace, "'" + name + "' is declared 'auto' from a braced list "
                                           "whose elements are '" + elem->describe() + "' and '" +
                                           e->describe() + "' - they must be one type");
            elem = e;
        }
    }
    return initializerListOf(elem, brace);
}

bool Parser::onlyImplicitConstructors(const std::string &tag) const {
    const std::vector<std::size_t> *set = overloadsOf(constructorKey(tag));
    if (set == nullptr) return true;
    for (std::size_t i = 0; i < set->size(); i++)
        if (!functions_[(*set)[i]].implicit) return false;
    return true;
}

// The elements go in as written; **a nested list waits behind a placeholder**
// for the constructor parameter it initialises ([over.match.list]/1): each row
// of `CMatrix<3, 3> X = { {1.0, 2.0, 3.0}, ... }` becomes the CVector<3>.
Parser::Signature Parser::listConstructor(const Type *plain, Init &in,
                                          std::vector<ExprPtr> &args, bool copyList,
                                          std::size_t pos) {
    std::vector<std::size_t> where;
    bool braced = false;
    for (std::size_t i = 0; i < in.items.size(); i++) {
        const std::size_t at = in.items[i].pos;
        if (in.items[i].isList) {
            args.push_back(bracedPlaceholder(std::move(in.items[i])));
            braced = true;
        } else {
            args.push_back(std::move(in.items[i].value));
        }
        where.push_back(at);
    }
    const std::string key = constructorKey(plain->tag());
    if (braced) buildBracedArguments(key, args, pos, plain);
    convertThroughConversionFunction(args, plain, !copyList, pos);
    Signature ctor = resolveOverload(key, args, pos);
    // [over.match.list]/1: copy-list-initialisation may not choose an explicit constructor.
    if (copyList && ctor.isExplicit)
        src_.fail(pos, "'" + plain->describe() + "' has a constructor taking these arguments "
                       "and it is 'explicit', so '= {...}' may not choose it - write the braces "
                       "without the '=', which asks for it by name");
    // [dcl.init.list]/7 holds for each element against the parameter it reaches.
    for (std::size_t i = 0; i < args.size() && i < ctor.params.size(); i++)
        checkNarrowing(ctor.params[i], *args[i], where[i], std::string());
    return ctor;
}

// `T{}` zeroes the scalar; `T{x}` is x converted, never narrowed; two elements are refused.
ExprPtr Parser::scalarFromList(const Type *to, Init &in, std::size_t pos) {
    if (in.items.size() > 1)
        src_.fail(in.items[1].pos, "'" + to->describe() + "{...}' takes one value, and this "
                                   "list has " + std::to_string(in.items.size()));
    if (in.items.size() == 1 && in.items[0].isList)
        src_.fail(in.items[0].pos, "'" + to->describe() + "{{...}}' - braces inside the braces "
                                   "of a scalar, which has no member to take them");
    if (in.items.empty()) {
        ExprPtr z;
        if (to->isPointer() || to->isNullPtr()) {
            z.reset(new Num(0LL));
            z->setType(types_.get(Kind::NullPtr));
        } else if (to->isFloating()) {
            z.reset(new Num(0.0L));
            z->setType(types_.doubleType());
        } else {
            z.reset(new Num(0LL));
            z->setType(types_.intType());
        }
        return convert(std::move(z), types_.withoutConst(to));
    }
    resolveOverloadPlaceholder(in.items[0].value, to, in.items[0].pos);
    ExprPtr v = decay(std::move(in.items[0].value));
    checkNarrowing(to, *v, in.items[0].pos, std::string());
    checkAssignable(*v, to, pos, "'" + to->describe() + "{...}'");
    return convert(std::move(v), types_.withoutConst(to));
}

// The object is a hidden local of this frame, built by the statements `emitInit` writes
// for a declaration, chained in front of its address; destroyed with the full expression.
ExprPtr Parser::aggregateTemporary(const Type *plain, Init &in, std::size_t pos) {
    const std::string name = "$list" + std::to_string(listTemps_++);
    const int off = declare(name, plain, pos);
    // Its value, where the list is constant - for a constant expression to read (foldObject). Asked before
    // emitInit, which takes the list's expressions for its stores.
    ConstObject constant;
    const bool folded = plain->kind() != Kind::Union && destructorOf(plain) == nullptr &&
                        constObjectFromInit(plain, in, 0, &constant);
    std::vector<StmtPtr> stmts;
    std::vector<InitStep> path;
    emitInit(name, path, plain, in, stmts);
    ExprPtr chain;
    for (std::size_t i = 0; i < stmts.size(); i++) {
        ExprStmt *one = dynamic_cast<ExprStmt *>(stmts[i].get());
        if (one == nullptr)
            src_.fail(pos, "'" + plain->describe() + "{...}' needs setup an expression cannot "
                           "carry - name a variable and initialise it with the braces instead");
        ExprPtr e = one->release();
        const Type *t = e->type();
        if (chain == nullptr) { chain = std::move(e); continue; }
        ExprPtr joined(new Comma(std::move(chain), std::move(e)));
        joined->setType(t);
        chain = std::move(joined);
    }
    if (destructorOf(plain) != nullptr) {
        const int guard = guardFlag();
        pendingTemps_.push_back(Temporary{ off, plain, guard });
        ExprPtr set = setGuard(guard, 1);
        if (chain == nullptr) chain = std::move(set);
        else {
            ExprPtr mark(new Comma(std::move(chain), std::move(set)));
            mark->setType(types_.intType());
            chain = std::move(mark);
        }
    }
    ExprPtr obj(Var::local(name, off));
    obj->setType(plain);
    if (chain == nullptr) {
        obj->setXvalue();
        obj->setDeclCategory(Expr::Category::Prvalue);
        if (folded) foldedAggregates_[obj.get()] = std::make_pair(name, constant);
        return obj;
    }
    ExprPtr at(new Unary('&', std::move(obj)));
    at->setType(types_.pointerTo(plain));
    ExprPtr both(new Comma(std::move(chain), std::move(at)));
    both->setType(types_.pointerTo(plain));
    ExprPtr made(new Unary('*', std::move(both)));
    made->setType(plain);
    made->setXvalue();
    made->setDeclCategory(Expr::Category::Prvalue);
    if (folded) foldedAggregates_[made.get()] = std::make_pair(name, constant);
    return made;
}

// The `$list` temporary an aggregateTemporary expression names: the Var itself, or `*(stores, &$list)`.
const Var *Parser::aggregateTemporaryVar(const Expr &e) {
    if (const Var *v = dynamic_cast<const Var *>(&e)) return v;
    const Unary *u = dynamic_cast<const Unary *>(&e);
    const Comma *c = u != nullptr && u->op() == '*' ? dynamic_cast<const Comma *>(&u->operand()) : nullptr;
    const Unary *at = c != nullptr ? dynamic_cast<const Unary *>(&c->right()) : nullptr;
    return at != nullptr && at->op() == '&' ? dynamic_cast<const Var *>(&at->operand()) : nullptr;
}

// [dcl.init.list]/3 in order: an initializer_list constructor, then the other
// constructors, an aggregate by its members, a scalar from one element.
ExprPtr Parser::listTemporaryFrom(const Type *to, Init &in, bool copyList, std::size_t pos) {
    const Type *plain = to->unqualified();
    if (plain->isVoid()) src_.fail(pos, "'void{...}' has no value");
    if (plain->isArray())
        src_.fail(pos, "'" + to->describe() + "{...}' - an array is not a type a temporary "
                       "can have; name a variable for it");
    if (!plain->isStructOrUnion()) return scalarFromList(plain, in, pos);
    if (isInitializerListType(plain)) return initializerListExpr(plain, in, pos);
    // [dcl.init.list]/3: an aggregate is initialised by its members, implicit
    // constructors from a member's class aside (`S{"made", 1}` with a
    // std::string member); an empty list value-initialises.
    if (!plain->tag().empty() && hasConstructors(plain->tag()) &&
        !(isAggregateClass(plain) && !in.items.empty())) {
        checkNotAbstract(plain, pos, "the temporary '" + plain->describe() + "{...}' would make");
        const Type *ilType = nullptr;
        if (!in.items.empty() && initializerListConstructor(plain, &ilType) != nullptr &&
            initializerListFits(ilType, in)) {
            std::vector<ExprPtr> args;
            args.push_back(initializerListExpr(ilType, in, pos));
            const Signature ctor = resolveOverload(constructorKey(plain->tag()), args, pos);
            applyDefaults(ctor, args, pos);
            return constructTemporary(plain, ctor, std::move(args), false, pos);
        }
        if (in.items.empty() || !onlyImplicitConstructors(plain->tag()) ||
            !hasMemberInitialiser(plain->tag())) {
            std::vector<ExprPtr> args;
            Signature ctor = listConstructor(plain, in, args, copyList, pos);
            applyDefaults(ctor, args, pos);
            if (ctor.access != Access::Public && !insideAccessOf(plain, ctor.access) &&
                !isFriendOf(plain))
                src_.fail(pos, "'" + plain->describe() + "' has no public constructor taking "
                               "these arguments - the one that matches is " +
                               (ctor.access == Access::Private ? "private" : "protected"));
            return constructTemporary(plain, ctor, std::move(args), in.items.empty() && ctor.implicit, pos);
        }
    }
    if (hasMemberInitialiser(plain->tag()))
        src_.fail(pos, "'" + plain->describe() + "' writes an initialiser on a member, so in "
                       "C++11 it is not an aggregate and a braced list cannot initialise it - "
                       "C++14 changed that rule and this compiler is C++11");
    return aggregateTemporary(plain, in, pos);
}

ExprPtr Parser::listTemporary(const Type *to, std::size_t pos) {
    Init in = parseInitialiser();
    return listTemporaryFrom(to, in, false, pos);
}

ExprPtr Parser::elementAddress(const std::string &temp, int slot, const Type *elem, std::size_t i) {
    const Type *pointer = types_.pointerTo(elem);
    ExprPtr base(Var::local(temp, slot));
    base->setType(pointer);
    if (i == 0) return base;
    const Type *chars = types_.pointerTo(types_.get(Kind::Char));
    ExprPtr asChars(new Cast(chars, std::move(base)));
    asChars->setType(chars);
    ExprPtr skip(new Num(static_cast<long long>(i) * static_cast<long long>(elem->size(target_))));
    skip->setType(types_.intType());
    ExprPtr at(new Binary(BinOp::Add, std::move(asChars), std::move(skip)));
    at->setType(chars);
    ExprPtr typed(new Cast(pointer, std::move(at)));
    typed->setType(pointer);
    return typed;
}

// **Copy-initialised from its clause where it lives**, [dcl.init.aggr]/2: a braced clause as
// `T x = {...}` chooses, an initializer_list constructor first; an expression by a constructor
// that is not explicit.
ExprPtr Parser::constructListedElement(const Type *made, Init &clause, ExprPtr at, std::size_t pos) {
    const std::size_t where = clause.pos != 0 ? clause.pos : pos;
    const std::string what = "an element of 'new " + made->describe() + "[n]{...}'";
    if (clause.isList && clause.items.empty())
        src_.fail(where, "'{}' for " + what + " is not supported yet - leave the element out of "
                         "the list, which value-initialises it");
    if (clause.isList && isAggregateClass(made))
        src_.fail(where, "an aggregate built from braces as " + what + " is not supported yet");
    std::vector<ExprPtr> args;
    const Type *ilType = nullptr;
    Signature ctor = Signature();
    if (clause.isList && initializerListConstructor(made, &ilType) != nullptr && initializerListFits(ilType, clause)) {
        args.push_back(initializerListExpr(ilType, clause, where));
        ctor = resolveOverload(constructorKey(made->tag()), args, where);
    } else if (clause.isList) {
        ctor = listConstructor(made, clause, args, true, where);
    } else {
        args.push_back(std::move(clause.value));
        convertThroughConversionFunction(args, made, false, where);
        {
            ConvertingOnly only(*this, true);
            ctor = resolveOverload(constructorKey(made->tag()), args, where);
        }
        if (ctor.isExplicit)
            src_.fail(where, "'" + made->describe() + "' is made from this by an 'explicit' "
                             "constructor, and " + what + " is copy-initialised, which may not "
                             "choose one");
    }
    applyDefaults(ctor, args, where);
    if (ctor.access != Access::Public && !insideAccessOf(made, ctor.access) && !isFriendOf(made))
        src_.fail(where, "'" + made->describe() + "' has no public constructor taking these "
                         "arguments - the one that matches is " +
                         (ctor.access == Access::Private ? "private" : "protected"));
    // [class.copy]/31: a temporary of the class itself is the element, built where the element lives - as an
    // array's listed element has been since 1.78 (1.80: `new T[2]{T('x'), T('y')}` copied each and destroyed
    // the temporaries). The copy or move constructor is the one chosen and checked above.
    if (!clause.isList && args.size() == 1 && constructInPlace(*args[0], made, at)) return std::move(args[0]);
    std::vector<ExprPtr> all;
    all.push_back(std::move(at));
    for (std::size_t i = 0; i < args.size(); i++) all.push_back(std::move(args[i]));
    std::vector<const Type *> full;
    full.push_back(types_.pointerTo(made));
    for (std::size_t i = 0; i < ctor.params.size(); i++) full.push_back(ctor.params[i]);
    return completeCall(made->tag(), ctor.symbol, nullptr, types_.get(Kind::Void), full, false,
                        where, std::move(all));
}

// A scalar never narrowed, [dcl.init.aggr]/2; an aggregate by its own braces, or a whole one.
ExprPtr Parser::listedElementValue(const Type *made, Init &clause) {
    const std::string what = "an element of 'new " + made->describe() + "[n]{...}'";
    const Type *plain = made->unqualified();
    if (plain->isArray())
        src_.fail(clause.pos, what + ", itself an array, is not supported yet");
    if (plain->isStructOrUnion()) {
        if (destructorOf(plain) != nullptr)
            src_.fail(clause.pos, what + " with a destructor and no constructor is not supported yet");
        if (clause.isList) return listTemporaryFrom(plain, clause, true, clause.pos);
        ExprPtr v = decay(std::move(clause.value));
        if (v->type() == nullptr || v->type()->unqualified() != plain)
            src_.fail(clause.pos, "braces left out around " + what + " are not supported yet - "
                                  "brace each element");
        return v;
    }
    if (clause.isList) return scalarFromList(plain, clause, clause.pos);
    ExprPtr v = decay(std::move(clause.value));
    checkNarrowing(plain, *v, clause.pos, std::string());
    checkAssignable(*v, plain, clause.pos, what);
    return convert(std::move(v), plain);
}

// **The conversion function on `from` that answers something usable.** The
// mirror of the converting constructor: that one is a constructor of the
// *target*, this one a member of the *source*.
// [over.ics.rank]/3 for the implied object: of two conversion functions to one type, the one the object may be
// given to wins, and of two it may, the one whose `this` is the less qualified - `operator T() const` before
// `operator T() const volatile` for an object that is not volatile, as <atomic> declares the pair. Negative
// for a, positive for b, 0 where neither is better. (Until 1.75 two to one type were none: `int x = a;` of a
// std::atomic<int> was "no conversion function".)
static int betterForObject(bool aConst, bool aVolatile, bool bConst, bool bVolatile, const Type *object) {
    const bool oc = object->isConst(), ov = object->isVolatile();
    const bool aFits = (!oc || aConst) && (!ov || aVolatile), bFits = (!oc || bConst) && (!ov || bVolatile);
    if (aFits != bFits) return aFits ? -1 : 1;
    const bool aWithinB = (!aConst || bConst) && (!aVolatile || bVolatile);
    const bool bWithinA = (!bConst || aConst) && (!bVolatile || aVolatile);
    if (aWithinB != bWithinA) return aWithinB ? -1 : 1;
    return 0;
}

const Parser::Signature *Parser::conversionFunction(const Type *from,
                                                    const Type *to,
                                                    bool allowExplicit) {
    const Type *plain = from->unqualified();
    if (!plain->isStructOrUnion()) return nullptr;
    if (to != nullptr) instantiateConversionTemplates(plain, to, peek().pos);

    // **Walked over the signature table and not over a member list**, because there is no member
    // list: the table is keyed by `Class::name`, so a class's conversions cannot be asked for
    // without already knowing what they convert to.
    const Signature *best = nullptr;
    bool exact = false;
    // Two inexact answers are none - unless an exact one comes later in the table (cl09).
    bool inexactTwice = false;
    bool boolFound = false;
    for (const Type *c = plain; c != nullptr; c = c->base()) {
        for (std::size_t k = 0; k < functions_.size(); k++) {
            const Signature &f = functions_[k];
            if (f.owner != c->tag()) continue;
            if (!isConversionName(f.name) || !f.params.empty()) continue;
            if (f.isExplicit && !allowExplicit) continue;
            // `operator const int &()` gives an lvalue of its referent, [class.conv.fct]/1.
            const Type *gives = f.returns->isReference() ? f.returns->referent() : f.returns;
            if (to == nullptr) {
                if (!gives->unqualified()->isScalar()) continue;
                if (gives->isBool()) {                   // the one asked for
                    if (best != nullptr && boolFound &&
                        betterForObject(best->constThis, best->volatileThis, f.constThis, f.volatileThis, from) <= 0)
                        continue;
                    best = &f;
                    boolFound = true;
                    inexactTwice = false;
                    continue;
                }
                if (boolFound) continue;
                if (best != nullptr) {
                    // the object chooses between two, whatever each gives ([over.match.conv]/1)
                    const int which =
                        betterForObject(best->constThis, best->volatileThis, f.constThis, f.volatileThis, from);
                    if (which > 0) best = &f;
                    else if (which == 0) inexactTwice = true;
                }
                else best = &f;
                continue;
            }
            if (gives->unqualified() == to->unqualified()) {
                // [over.match.best]/1: a template's specialization loses a tie to a function that is not one.
                if (exact && best->fromTemplate != f.fromTemplate) {
                    if (best->fromTemplate) best = &f;
                    continue;
                }
                if (exact) {                             // two exact answers, unless the object tells them apart
                    const int which = betterForObject(best->constThis, best->volatileThis, f.constThis, f.volatileThis, from);
                    if (which == 0) return nullptr;
                    if (which > 0) best = &f;
                    continue;
                }
                best = &f;
                exact = true;
                continue;
            }
            if (exact) continue;
            if (!gives->unqualified()->isScalar() || !to->isScalar()) continue;
            if (best != nullptr) {
                const Type *had = best->returns->isReference() ? best->returns->referent() : best->returns;
                const int which = had->unqualified() == gives->unqualified()
                    ? betterForObject(best->constThis, best->volatileThis, f.constThis, f.volatileThis, from) : 0;
                if (which > 0) best = &f;
                else if (which == 0) inexactTwice = true;
                continue;
            }
            best = &f;
        }
    }
    return exact || !inexactTwice ? best : nullptr;
}

// [dcl.init.ref]/5: the conversion function of class `from` that gives an lvalue a reference to `referent` binds
// directly - one returning `U &`, U the referent's type and no more qualified - or null for none, or for two that
// nothing tells apart. `int &r = std::ref(n);`.
const Parser::Signature *Parser::referenceConversion(const Type *from, const Type *referent) {
    if (!from->unqualified()->isStructOrUnion()) return nullptr;
    const Signature *via = nullptr;
    bool several = false;
    for (const Type *c = from->unqualified(); c != nullptr; c = c->base())
        for (std::size_t k = 0; k < functions_.size(); k++) {
            const Signature &f = functions_[k];
            if (f.owner != c->tag() || !isConversionName(f.name) || !f.params.empty() || f.isExplicit) continue;
            if (!f.returns->isReference() || f.returns->isRValueReference()) continue;
            const Type *gives = f.returns->referent();
            if (gives->unqualified() != referent->unqualified()) continue;
            if ((gives->isConst() && !referent->isConst()) || (gives->isVolatile() && !referent->isVolatile()))
                continue;
            if ((from->isConst() && !f.constThis) || (from->isVolatile() && !f.volatileThis)) continue;
            if (via != nullptr && via->returns == f.returns && via->constThis == f.constThis &&
                via->volatileThis == f.volatileThis)
                continue;                                   // the same function, inherited twice
            // of two, the one whose `this` is the less qualified, as an object not const is given
            if (via != nullptr) {
                const bool fLess = (!f.constThis || via->constThis) && (!f.volatileThis || via->volatileThis);
                const bool viaLess = (!via->constThis || f.constThis) && (!via->volatileThis || f.volatileThis);
                if (fLess == viaLess) several = true;
                else if (fLess) via = &f;
                continue;
            }
            via = &f;
        }
    return several ? nullptr : via;
}

// **The one conversion this class has to a number or pointer**, or null for
// none or several; an `explicit` one is no candidate for a built-in operand
// ([over.match.oper]/7, [class.conv.fct]/2); contextual ones ask elsewhere.
const Parser::Signature *Parser::soleNumericConversion(const Type *from) {
    const Type *plain = from->unqualified();
    if (!plain->isStructOrUnion()) return nullptr;
    const Signature *only = nullptr;
    for (const Type *c = plain; c != nullptr; c = c->base())
        for (std::size_t k = 0; k < functions_.size(); k++) {
            const Signature &f = functions_[k];
            if (f.owner != c->tag()) continue;
            if (!isConversionName(f.name) || !f.params.empty() || f.isExplicit) continue;
            if (!f.returns->unqualified()->isScalar()) continue;
            if (only != nullptr) {
                // [over.match.conv]/1: the conversion functions compete as candidates, the object their one
                // argument - so of `operator int *()` and `operator const int *() const` an object that is not
                // const takes the first, whatever each gives (1.80: two that gave different types were none,
                // and `*p`, `p[0]` and `p + 1` of such a class were refused).
                const int which = betterForObject(only->constThis, only->volatileThis, f.constThis, f.volatileThis, from);
                if (which == 0) return nullptr;
                if (which > 0) only = &f;
                continue;
            }
            only = &f;
        }
    return only;
}

// **[over.built]: the operand of a built-in unary operator or subscript that is a class** is converted by
// its one conversion function to a number or a pointer - `-n`, `~n`, `*p`, `p[i]` (1.80: each was refused
// with "needs a number, not 'struct N'", where the binary operators had converted since 1.2x). The operand
// as it is where the class has none, for the caller to say what it wanted.
ExprPtr Parser::builtinOperand(ExprPtr v, std::size_t pos) {
    if (v->type() == nullptr || !v->type()->unqualified()->isStructOrUnion()) return v;
    const Signature *how = soleNumericConversion(v->type());
    if (how == nullptr) return v;
    if ((v->type()->isConst() && !how->constThis) || (v->type()->isVolatile() && !how->volatileThis)) return v;
    const Type *object = v->type();
    return memberCallWith(std::move(v), object, how->name, pos, std::vector<ExprPtr>());
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
    // **[conv.bool]: a pointer to member is true when it is not null** - `if (pm)`, `!pm` (R4). Its
    // null is not the number 0 (a data member pointer's is -1), which the comparison knows.
    if (e->type() != nullptr && (e->type()->isMemberPointer() || e->type()->isMemberFunctionPointer())) {
        ExprPtr none(new Num(0LL));
        none->setType(types_.intType());
        e = comparison(BinOp::Ne, std::move(e), std::move(none), pos);
    }
    requireScalar(*e, pos, what);
    return e;
}

// **[over.ics.user]: the constructor that could make `to` out of `from`.**
const Parser::Signature *Parser::convertingConstructor(const Type *to,
                                                       const Expr &from,
                                                       std::size_t pos, bool directInit) {
    const Type *plain = to->unqualified();
    if (!plain->isStructOrUnion()) return nullptr;
    std::vector<const Type *> one(1, from.type());
    instantiateConstructorTemplates(plain, one, pos);
    const std::vector<std::size_t> *set = overloadsOf(constructorKey(plain->tag()));
    if (set == nullptr) return nullptr;

    // **Ranked, not counted**: once a constructor template has been instantiated for an int,
    // `W(const int &)` stands beside `W(const double &)` for a 2.5, and [over.match.best] picks.
    const Signature *found = nullptr;
    Rank foundRank = Rank::None;
    bool ambiguous = false;
    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &c = functions_[(*set)[k]];
        // Direct-initialisation - a cast - may take an explicit one, [over.match.ctor].
        // [class.conv.ctor]/1: one argument, the parameters after it defaulted - `basic_string(const C *, const A & = A())`.
        if ((c.isExplicit && !directInit) || c.params.empty() || leastArguments(c) > 1) continue;
        const Type *want = c.params[0];
        const Type *bare = want->isReference() ? want->pointee()->unqualified()
                                               : want->unqualified();
        if (bare == plain) continue;                 // the copy constructor
        const Rank r = rankArgument(from, want);
        if (r == Rank::None) continue;
        if (found == nullptr) { found = &c; foundRank = r; continue; }
        const std::vector<Rank> a(1, r), b(1, foundRank);
        if (betterCandidate(a, b, c, *found, false, false)) {
            found = &c; foundRank = r; ambiguous = false;
        } else if (!betterCandidate(b, a, *found, c, false, false)) {
            ambiguous = true;
        }
    }
    return ambiguous ? nullptr : found;
}

// **The conversion an argument needs to become the parameter's class**, or
// null where none is needed or possible.
ExprPtr Parser::userConversion(const Type *param, ExprPtr &arg, std::size_t pos) {
    const Type *want = param->isReference() ? param->pointee() : param;
    const Type *plain = want->unqualified();
    if (!plain->isStructOrUnion()) return nullptr;
    // [dcl.init.ref]/5: the temporary is bound by a reference to const or by an rvalue reference.
    if (param->isReference() && !param->isRValueReference() && !want->isConst()) return nullptr;
    if (arg->type() == nullptr) return nullptr;
    if (arg->type()->unqualified() == plain) return nullptr;
    // [dcl.init.ref]/5: a reference binds a derived object directly and asks no constructor - not even a
    // template's `const Tagged<U> &` at the base's own U, which would come back here for its own argument.
    if (param->isReference() && arg->type()->unqualified()->isStructOrUnion() &&
        accessibleBaseOffset(arg->type()->unqualified(), plain) >= 0)
        return nullptr;

    const Signature *ctor = convertingConstructor(plain, *arg, pos);
    if (ctor != nullptr && conversionOverConstructor(plain, *arg, *ctor, pos) != nullptr) ctor = nullptr;
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
    applyDefaults(*ctor, one, pos);
    return constructTemporary(plain, *ctor, std::move(one), false, pos);
}

// `A a = b;` for a b of another class - a variable's initialiser, an element's: b's conversion function to A is
// a candidate beside A's constructors. Where it is the better one the argument becomes what it gives.
void Parser::copyFromOtherClass(const Type *cls, std::vector<ExprPtr> &args, std::size_t pos) {
    if (args.size() != 1 || args[0]->type() == nullptr) return;
    const Type *plain = cls->unqualified();
    const Type *from = args[0]->type();
    if (!from->unqualified()->isStructOrUnion() || from->unqualified() == plain ||
        accessibleBaseOffset(from->unqualified(), plain) >= 0)
        return;
    const Signature *ctor = convertingConstructor(plain, *args[0], pos);
    if (ctor == nullptr) return;
    if (const Signature *how = conversionOverConstructor(plain, *args[0], *ctor, pos)) {
        const std::string conv = how->name;
        args[0] = memberCallWith(std::move(args[0]), from, conv, pos, std::vector<ExprPtr>());
    }
}

// [over.match.copy]/1: copy-initialising an A from a B, A's constructor from a B and B's own conversion function
// to A are both candidates, the reference to B each binds deciding between them ([over.ics.rank]/3.2.6) - or, the
// same, neither, and the initialisation is ambiguous. The conversion function where it is the better one, null
// where the constructor `ctor` stays; refused where nothing tells them apart. Asked of an argument, a returned
// value and an assigned one since 1.6x, and since 1.80 of a variable's initialiser too: `A a = b;` took the
// constructor without asking.
const Parser::Signature *Parser::conversionOverConstructor(const Type *plain, const Expr &given_, const Signature &chosen,
                                                           std::size_t pos, bool *compared) {
    if (compared != nullptr) *compared = false;
    const Expr *arg = &given_;
    const Signature *ctor = &chosen;
    if (ctor != nullptr && !ctor->fromTemplate && !ctor->params.empty() &&
        arg->type()->unqualified()->isStructOrUnion()) {
        const Type *source = arg->type()->unqualified();
        // B's one conversion function to A by value, declared in B itself and no template.
        const Signature *how = nullptr;
        int found = 0;
        for (const Signature &f : functions_)
            if (f.owner == source->tag() && isConversionName(f.name) && f.params.empty() && !f.isExplicit &&
                !f.returns->isReference() && f.returns->unqualified() == plain) {
                how = &f;
                found++;
            }
        const Type *taken = ctor->params[0]->isReference() ? ctor->params[0]->referent() : ctor->params[0];
        const unsigned given = (arg->type()->isConst() ? 1u : 0u) | (arg->type()->isVolatile() ? 2u : 0u);
        const unsigned byConversion = found == 1 ? (how->constThis ? 1u : 0u) | (how->volatileThis ? 2u : 0u) : 0u;
        if (found == 1 && !how->fromTemplate && how->refThis == RefQual::None && taken->unqualified() == source &&
            (given & ~byConversion) == 0) {
            if (compared != nullptr) *compared = true;
            const unsigned byCtor = (taken->isConst() ? 1u : 0u) | (taken->isVolatile() ? 2u : 0u);
            if (!ctor->params[0]->isReference() || byCtor == byConversion ||
                ((byCtor & ~byConversion) != 0 && (byConversion & ~byCtor) != 0))
                src_.fail(pos, "converting '" + source->describe() + "' to '" + plain->describe() + "' is ambiguous: "
                               "the constructor " + describeSignature(*ctor) + " and the conversion function " +
                               describeSignature(*how) + " are equally good - [over.match.best]");
            if ((byConversion & ~byCtor) == 0) return how;
        }
    }
    return nullptr;
}

// Whether any constructor of `cls` takes these arguments as they stand - the
// count and rank checks resolveOverload makes, without its refusal.
bool Parser::constructorViable(const Type *cls, const std::vector<ExprPtr> &args,
                               std::size_t pos) {
    instantiateConstructorTemplates(cls->unqualified(), args, pos);
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
    if (constructorViable(plain, args, pos)) return false;
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
    // [expr.unary.noexcept]/3: a constructor called is a call of the expression like any other, also
    // the one an implicit conversion or a cast calls - `noexcept(c = 1)` with a `C(int)` that may throw
    // is false (1.77: only a constructor found by overload resolution at the call was counted).
    if (!ctor.isNoexcept) mayThrow_++;
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
    made->setDeclCategory(Expr::Category::Prvalue);
    return made;
}

// ---------------------------------------------------------------- new and delete
// **The four operator functions are called by name**, measured at -O0.
ExprPtr Parser::dynamicCastToVoid(ExprPtr v, const Type *to) {
    // **The Microsoft ABI keeps no offset-to-top, and does not do this
    // inline.**
    if (target_.microsoftNames()) {
        const Type *from = v->type();
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
        ExprPtr typed(new Cast(from, std::move(object)));
        typed->setType(from);
        ExprPtr vf(new Cast(voidPtr, msToVfptr(std::move(typed), from->pointee()->unqualified())));
        vf->setType(voidPtr);
        args.push_back(std::move(vf));
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

    if (to->isReference()) return dynamicCastReference(std::move(v), to, pos);
    return dynamicCastFrom(std::move(v), to, pos);
}

// [expr.dynamic.cast]/9: the pointer form on the operand's address, null
// answering `__cxa_bad_cast()` (as clang); Microsoft's `__RTDynamicCast`, told
// it is a reference, throws std::bad_cast itself (as cl), never null.
ExprPtr Parser::dynamicCastReference(ExprPtr v, const Type *to, std::size_t pos) {
    if (to->isRValueReference())
        src_.fail(pos, "'dynamic_cast' to an rvalue reference is not supported yet - the "
                       "lvalue reference form works");
    const Type *referent = to->referent();
    if (!isGlvalue(*v) || !v->type()->unqualified()->isStructOrUnion())
        src_.fail(pos, "'dynamic_cast<" + to->describe() + ">' needs an object of a class to "
                       "ask about, and this is '" + v->type()->describe() + "'");
    const Type *ptr = types_.pointerTo(referent);
    const Type *object = v->type();
    ExprPtr addr(new Unary('&', std::move(v)));
    addr->setType(types_.pointerTo(object));
    // [expr.unary.noexcept]/3: only the cast that needs a run-time check can throw.
    if (publicBaseOffset(object->unqualified(), referent->unqualified()) < 0) mayThrow_++;
    if (target_.microsoftNames()) {
        ExprPtr obj(new Unary('*', dynamicCastFrom(std::move(addr), ptr, pos, true)));
        obj->setType(referent);
        return obj;
    }
    ExprPtr answer = dynamicCastFrom(std::move(addr), ptr, pos);
    const int slot = allocateFrameSlot(ptr);
    const std::string temp = ".dynref" + std::to_string(newTemps_++);
    ExprPtr held(Var::local(temp, slot));
    held->setType(ptr);
    ExprPtr save(new Assign(std::move(held), std::move(answer)));
    save->setType(ptr);
    ExprPtr test(Var::local(temp, slot));
    test->setType(ptr);
    ExprPtr one(new Num(1LL));
    one->setType(types_.intType());
    ExprPtr fail = runtimeCall("__cxa_bad_cast", types_.get(Kind::Void), std::vector<ExprPtr>());
    ExprPtr alsoOne(new Num(1LL));
    alsoOne->setType(types_.intType());
    ExprPtr failed(new Comma(std::move(fail), std::move(alsoOne)));
    failed->setType(types_.intType());
    ExprPtr check(new Conditional(std::move(test), std::move(one), std::move(failed)));
    check->setType(types_.intType());
    ExprPtr first(new Comma(std::move(save), std::move(check)));
    first->setType(types_.intType());
    ExprPtr again(Var::local(temp, slot));
    again->setType(ptr);
    ExprPtr both(new Comma(std::move(first), std::move(again)));
    both->setType(ptr);
    ExprPtr obj(new Unary('*', std::move(both)));
    obj->setType(referent);
    return obj;
}

ExprPtr Parser::dynamicCastFrom(ExprPtr v, const Type *to, std::size_t pos, bool reference) {
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
    // **[expr.dynamic.cast]/3 and /5: the same class, or an upcast, is not a question** - asked of any
    // class, polymorphic or not (1.77: the polymorphism was asked for first), and of a base reachable from here.
    if (!toVoid) {
        const Type *target = to->pointee()->unqualified();
        for (const Type::BaseSpec &b : source->bases())
            if (b.type->unqualified() == target && accessibleBaseOffset(source, target) < 0)
                src_.fail(pos, "'" + target->describe() + "' is an inaccessible base of '" + source->describe() +
                               "', so 'dynamic_cast' cannot convert to it - [expr.dynamic.cast]/5");
        if (source == target || publicBaseOffset(source, target) >= 0) {
            if ((from->pointee()->isConst() && !to->pointee()->isConst()) ||
                (from->pointee()->isVolatile() && !to->pointee()->isVolatile()))
                src_.fail(pos, "'dynamic_cast' from '" + from->describe() + "' to '" + to->describe() +
                               "' would cast away a qualifier - [expr.dynamic.cast]/1");
            return convert(std::move(v), to);
        }
    }
    // [expr.dynamic.cast]/6: the operand's class must be polymorphic, because
    // the answer is read out of the vtable and a class without one has nothing
    // to read.
    if (!source->polymorphic())
        src_.fail(pos, "'" + source->describe() + "' has no virtual function, "
                       "so an object of it carries nothing that says what it "
                       "really is - 'dynamic_cast' has nothing to ask");
    if (toVoid) return dynamicCastToVoid(std::move(v), to);

    const Type *target = to->pointee()->unqualified();

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
            std::vector<MicrosoftBaseClass> rows;
            int hierarchy = 0;
            if (microsoftClassRttiNames(cls, &names, &why) && msRttiRows(cls, &rows, &hierarchy))
                sym = names.descriptor;
        } else {
            sym = emitClassTypeInfo(cls, cls->tag(), pos);
        }
        // Itanium writes __vmi_class_type_info for any class; Microsoft's hierarchy is one base deep here.
        if (sym.empty())
            src_.fail(pos, "'dynamic_cast' with '" + cls->describe() + "', which has more than "
                           "one base and a virtual or a repeated one among them, is not supported yet "
                           "on x86_64-windows - its RTTI would carry a vbtable displacement or an "
                           "ambiguity flag not measured yet; every shape works on the other targets");
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
    // **Where the operand's vfptr is in a virtual base**, the runtime is handed that and how far it is
    // from the operand, VfDelta, as cl computes them (M3).
    ExprPtr vfSave;
    int vfSlot = 0;
    std::string vfTemp;
    if (ms && msVfptrInVirtualBase(source)) {
        vfSlot = allocateFrameSlot(voidPtr);
        vfTemp = ".dvf" + std::to_string(newTemps_++);
        ExprPtr typed(new Cast(from, std::move(object)));
        typed->setType(from);
        ExprPtr vf(new Cast(voidPtr, msToVfptr(std::move(typed), source)));
        vf->setType(voidPtr);
        ExprPtr held(Var::local(vfTemp, vfSlot));
        held->setType(voidPtr);
        vfSave.reset(new Assign(std::move(held), std::move(vf)));
        vfSave->setType(voidPtr);
        object.reset(Var::local(vfTemp, vfSlot));
        object->setType(voidPtr);
    }
    args.push_back(std::move(object));

    // **The two runtimes take their arguments in a different order and a
    // different number**, which is the whole of the difference at the call.
    if (ms) {
        ExprPtr delta(new Num(0LL));
        delta->setType(types_.get(Kind::Int));
        if (vfSave != nullptr) {
            const Type *chars = types_.pointerTo(types_.get(Kind::Char));
            ExprPtr a(Var::local(vfTemp, vfSlot));
            a->setType(voidPtr);
            ExprPtr ac(new Cast(chars, std::move(a)));
            ac->setType(chars);
            ExprPtr b(Var::local(temp, slot));
            b->setType(voidPtr);
            ExprPtr bc(new Cast(chars, std::move(b)));
            bc->setType(chars);
            ExprPtr diff(new Binary(BinOp::Sub, std::move(ac), std::move(bc)));
            diff->setType(ptrdiffType());
            delta.reset(new Cast(types_.get(Kind::Int), std::move(diff)));
            delta->setType(types_.get(Kind::Int));
        }
        args.push_back(std::move(delta));
    }
    args.push_back(typeInfoAddress(sourceName));
    args.push_back(typeInfoAddress(targetName));
    if (ms) {
        ExprPtr isReference(new Num(reference ? 1LL : 0LL));
        isReference->setType(types_.get(Kind::Int));
        args.push_back(std::move(isReference));
    } else {
        ExprPtr hint(new Num(-1LL));
        hint->setType(types_.get(Kind::LongLong));
        args.push_back(std::move(hint));
    }

    ExprPtr asked = runtimeCall(ms ? "__RTDynamicCast" : "__dynamic_cast",
                                to, std::move(args));
    if (vfSave != nullptr) {
        ExprPtr both(new Comma(std::move(vfSave), std::move(asked)));
        both->setType(to);
        asked = std::move(both);
    }

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

// The statements a `throw` lowers to are a block of expression statements - stores, guard flags and
// calls - so an expression can carry them as one comma chain. Anything else in there is refused.
ExprPtr Parser::statementsAsExpression(StmtPtr s, std::size_t pos) {
    if (ExprStmt *one = dynamic_cast<ExprStmt *>(s.get())) return one->release();
    Block *b = dynamic_cast<Block *>(s.get());
    if (b == nullptr)
        src_.fail(pos, "this 'throw' builds a statement an expression cannot carry - write it as "
                       "a statement of its own");
    ExprPtr chain;
    for (const StmtPtr &x : b->body()) {
        ExprStmt *one = dynamic_cast<ExprStmt *>(x.get());
        if (one == nullptr)
            src_.fail(pos, "this 'throw' builds a statement an expression cannot carry - write it "
                           "as a statement of its own");
        ExprPtr e = one->release();
        if (chain == nullptr) { chain = std::move(e); continue; }
        ExprPtr both(new Comma(std::move(chain), std::move(e)));
        both->setType(types_.get(Kind::Void));
        chain = std::move(both);
    }
    if (chain == nullptr) src_.fail(pos, "this 'throw' lowered to nothing");
    chain->setType(types_.get(Kind::Void));
    return chain;
}

// **[expr.throw]: `throw e` is a void expression** (an arm of `?:`, the operand
// of noexcept), lowered as the statement is: chained, a rethrow where it has
// one, [except.spec]/9's terminate where it puts it.
ExprPtr Parser::throwExpression() {
    const std::size_t tpos = peek().pos;
    expect("throw");
    throwsSeen_++;
    mayThrow_++;
    if (peek().is(";") || peek().is(")") || peek().is(",") || peek().is(":") ||
        peek().is("]") || peek().is("}"))
        return rethrowCall();
    ExprPtr value = decay(assign());
    checkThrowOperand(*value, tpos);
    if (inNoexceptFunction_ && !inTryBody_ && !inHandlerBody_) {
        ExprPtr both(new Comma(std::move(value), terminateCall()));
        both->setType(types_.get(Kind::Void));
        return both;
    }
    return statementsAsExpression(throwStatement(std::move(value), tpos), tpos);
}

// [except.spec]/9 names std::terminate, and a handler set for it has to be the one that runs.
ExprPtr Parser::terminateCall() {
    return runtimeCall(target_.microsoftNames() ? "terminate" : "_ZSt9terminatev",
                       types_.get(Kind::Void), std::vector<ExprPtr>());
}

ExprPtr Parser::rethrowCall() {
    if (!target_.microsoftNames())
        return runtimeCall("__cxa_rethrow", types_.get(Kind::Void), std::vector<ExprPtr>());
    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    std::vector<ExprPtr> args;
    for (int i = 0; i < 2; i++) {
        ExprPtr zero(new Num(0LL));
        zero->setType(types_.intType());
        ExprPtr null(new Cast(voidPtr, std::move(zero)));
        null->setType(voidPtr);
        args.push_back(std::move(null));
    }
    return runtimeCall("_CxxThrowException", types_.get(Kind::Void), std::move(args));
}

// **[class.copy]/31-32: `throw x;` moves from x where it may**: an xvalue or
// prvalue, a parameter, or an automatic object (no catch parameter) whose scope
// ends in the innermost try or has none; a class by address is read via `*p`.
bool Parser::throwOperandMoves(const Expr &value) const {
    if (value.isXvalue() || !isGlvalue(value)) return true;
    const Var *v = dynamic_cast<const Var *>(&value);
    bool throughAddress = false;
    if (v == nullptr)
        if (const Unary *u = dynamic_cast<const Unary *>(&value))
            if (u->op() == '*') {
                v = dynamic_cast<const Var *>(&u->operand());
                throughAddress = true;
            }
    if (v == nullptr || !v->isLocal()) return false;
    const Local *l = findLocal(v->name());
    if (l == nullptr || l->offset != v->offset() || l->isCatchParameter || !l->staticName.empty())
        return false;
    if (l->isParameter) return !throughAddress || l->byValueByAddress;
    if (throughAddress) return false;
    return !inTryBody_ || l->serial > tryBodySerial_;
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
    const Signature *cc = nullptr;
    if (thrown->isStructOrUnion()) {
        if (throwOperandMoves(*value)) cc = moveConstructorOf(thrown);
        if (cc != nullptr) value->setXvalue();
        else cc = copyConstructorOf(thrown);
    }
    const Type *thrownPtr = types_.pointerTo(thrown);
    ExprPtr heldAt(Var::local(temp, slot));
    heldAt->setType(thrown);
    ExprPtr slotAt(new Unary('&', std::move(heldAt)));
    slotAt->setType(thrownPtr);
    // [class.copy]/31: a prvalue of the class is the exception object, built where it will stay.
    if (cc != nullptr && constructInPlace(*value, thrown, slotAt)) {
        store = std::move(value);
    } else if (cc != nullptr) {
        markUsed(cc);
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

// [except.throw]/3,5: the exception object is copy-initialised from the
// operand, so its type is complete, not abstract, not void; a class's copy or
// move and destructor must be accessible, elided or not.
void Parser::checkThrowOperand(const Expr &value, std::size_t pos) {
    const Type *thrown = value.type()->unqualified();
    if (thrown->isVoid())
        src_.fail(pos, "an expression of type 'void' cannot be thrown - [except.throw]/3");
    const Type *named = thrown->isPointer() ? thrown->pointee()->unqualified() : thrown;
    if (!named->isVoid() && !named->isFunction() && !named->isComplete())
        src_.fail(pos, "'" + thrown->describe() + "' cannot be thrown: '" + named->describe() +
                       "' is an incomplete type - [except.throw]/3");
    if (!thrown->isStructOrUnion()) return;
    if (thrown->abstract())
        src_.fail(pos, "'" + thrown->describe() + "' is an abstract class, and no object of it can be "
                       "thrown - [class.abstract]/3");
    auto reachable = [&](const Signature *s) {
        return s == nullptr || s->access == Access::Public || insideAccessOf(thrown, s->access) ||
               isFriendOf(thrown);
    };
    // A local lvalue is moved from where it can be ([class.copy]/32), so an accessible move answers too.
    const Signature *mover = moveConstructorOf(thrown);
    const Signature *copier = mover != nullptr && reachable(mover) ? mover : copyConstructorOf(thrown);
    const Signature *dtor = destructorOf(thrown);
    for (const Signature *s : { copier, dtor })
        if (!reachable(s))
            src_.fail(pos, "'" + thrown->describe() + "' cannot be thrown: its " +
                           (s == dtor ? "destructor" : "copy constructor") + " is " +
                           (s->access == Access::Private ? "private" : "protected") +
                           " - [except.throw]/5 asks for it whether or not the copy is elided");
}

StmtPtr Parser::throwStatement(ExprPtr value, std::size_t pos) {
    checkThrowOperand(*value, pos);
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
    const Signature *cc = throwOperandMoves(*value) ? moveConstructorOf(thrown) : nullptr;
    if (cc != nullptr) value->setXvalue();
    else cc = copyConstructorOf(thrown);
    ExprPtr heldAt(Var::local(temp, slot));
    heldAt->setType(voidPtr);
    ExprPtr objectAt(new Cast(thrownPtr, std::move(heldAt)));
    objectAt->setType(thrownPtr);
    // [class.copy]/31: a prvalue of the class is the exception object, built where it will stay.
    if (cc != nullptr && constructInPlace(*value, thrown, objectAt)) {
        store = std::move(value);
    } else if (cc != nullptr) {
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
    struct Global { bool &flag; bool was; ~Global() { flag = was; } } global{ globalAllocation_, globalAllocation_ };
    globalAllocation_ = globalNewDelete_;
    globalNewDelete_ = false;
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
    // `new (p) T`: the caller's storage; a throwing ctor frees nothing (/7)
    const bool reservedPlacement = placement != nullptr && !nothrow;

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
        // **`new T[n][3][4]` makes n arrays of `T[3][4]`** - [expr.new]/6: every bound after the
        // first is a constant, and the pointer answered points at the first of those arrays.
        std::vector<long long> inner;
        while (peek().is("[")) {
            const std::size_t bpos = peekAt(1).pos;
            at_++;
            const long long b = constantExpression("an array bound after the first in 'new'");
            if (b <= 0)
                src_.fail(bpos, "an array bound after the first in 'new' must be a constant "
                                "above zero - [expr.new]/6");
            inner.push_back(b);
            expect("]");
        }
        for (std::size_t k = inner.size(); k-- > 0; ) made = types_.arrayOf(made, inner[k]);
        if (!inner.empty()) {
            const Type *leaf = made->innermostElement()->unqualified();
            if (leaf->isStructOrUnion() && !leaf->tag().empty() &&
                (hasConstructors(leaf->tag()) || destructorOf(leaf) != nullptr))
                src_.fail(pos, "an array of arrays of '" + leaf->describe() + "' from 'new' is not "
                               "supported yet - its elements need constructing, and only the "
                               "first dimension of those is built here");
        }
    }

    // [expr.new]/2: `new auto(e)` is given e's type, as `auto x(e);` would be - one value in
    // parentheses, read here for its type and below again as the initialiser.
    if (mentionsDeduced(made)) {
        if (array || !peek().is("("))
            src_.fail(pos, "'new " + made->describe() + "' takes its type from one value in "
                           "parentheses after it, [expr.new]/2" +
                           (array ? std::string(", and an array has none") : std::string()));
        const std::size_t resume = at_;
        const Type *from = nullptr;
        {
            Discarded held(this);
            at_++;
            ExprPtr init = assign();
            if (!peek().is(")"))
                src_.fail(peek().pos, "'new " + made->describe() + "(...)' takes one value, "
                                      "[expr.new]/2");
            from = deductionArgType(*init);
        }
        at_ = resume;
        made = deduceAutoFrom(made, from, "new " + made->describe(), pos);
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
                             hasConstructors(made->tag());
    std::vector<ExprPtr> ctorArgs;
    bool hasInit = false;
    ExprPtr init;
    // **`new T{}` is `new T()`**, [dcl.init]/11; `new T{...}` list-initialises the object,
    // [expr.new]/15 - a constructor's arguments, or an aggregate or scalar made as `T{...}`.
    const bool braces = peek().is("{");
    // **`new T[n]{a, b}` initialises the array as an aggregate** - [expr.new]/17 and
    // [dcl.init.aggr]/2,7: each listed element copy-initialised from its clause, the rest
    // value-initialised (R28).
    std::vector<Init> elements;
    if (braces && !peekAt(1).is("}")) {
        Init in = parseInitialiser();
        if (array) elements = std::move(in.items);
        else if (constructed) listConstructor(made, in, ctorArgs, false, pos);
        else init = listTemporaryFrom(made, in, false, pos);
        hasInit = true;
    } else if (braces) {
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
            // **`new T(std::forward<A>(a)...)` of a scalar T** - read as call arguments are, so a
            // pack expands: one value initialises, none value-initialises ([temp.variadic]/7).
            const std::size_t lpos = peek().pos;
            std::vector<ExprPtr> values;
            parseArguments(values);
            if (values.size() > 1)
                src_.fail(lpos, "more than one value in a new-expression "
                                "needs a constructor, which '" + made->describe() +
                                "' does not have");
            if (values.size() == 1) {
                if (isBracedPlaceholder(*values[0]))
                    src_.fail(lpos, "a braced list in the parentheses of 'new " +
                                    made->describe() + "(...)' is not supported yet");
                init = std::move(values[0]);
            }
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
    // **[expr.new]/7: a negative count, or one too big for a size_t with its
    // cookie, throws std::bad_array_new_length** (via <new>'s helper, once
    // read); a literal count that fits is not checked.
    ExprPtr countKept;
    ExprPtr tooMany;
    ExprPtr tooManyThrows;
    // Every element listed, by a constant count: no element is left to the default constructor.
    bool allListed = false;
    if (array) {
        ExprPtr given = decay(std::move(count));
        const Num *literal = dynamic_cast<const Num *>(given.get());
        // A constant count below the clauses listed is refused, as clang refuses it; a count known
        // only when it runs throws below, [expr.new]/7.
        if (literal != nullptr && literal->value() >= 0 &&
            static_cast<unsigned long long>(literal->value()) < elements.size())
            src_.fail(elements[static_cast<std::size_t>(literal->value())].pos,
                      "excess elements: 'new " + made->describe() + "[" +
                      std::to_string(literal->value()) + "]' lists " +
                      std::to_string(elements.size()) + " of them");
        allListed = literal != nullptr && !elements.empty() &&
                    static_cast<unsigned long long>(literal->value()) == elements.size();
        const std::vector<std::size_t> *thrower = overloadsOf("std::__throw_bad_array_new_length");
        const Type *ct = given->type() != nullptr ? given->type()->unqualified() : nullptr;
        if (thrower != nullptr && !thrower->empty() && ct != nullptr && ct->isInteger() &&
            (literal == nullptr || literal->value() < 0)) {
            const unsigned sizeBytes = static_cast<unsigned>(sizeT->size(target_));
            const unsigned long long most = sizeBytes >= 8 ? ~0ULL : (1ULL << (8 * sizeBytes)) - 1;
            const unsigned long long element = static_cast<unsigned long long>(made->size(target_));
            const unsigned long long limit = element == 0 ? most
                                           : (most - static_cast<unsigned long long>(cookie)) / element;
            const int keptSlot = allocateFrameSlot(ct);
            const std::string keptTemp = ".newk" + std::to_string(newTemps_++);
            auto kept = [&]() { ExprPtr e(Var::local(keptTemp, keptSlot)); e->setType(ct); return e; };
            countKept.reset(new Assign(kept(), std::move(given)));
            countKept->setType(ct);
            const Type *boolT = types_.get(Kind::Bool);
            // Compared in the count's own width where that is wider than size_t's - a long long on the
            // C6747 - since narrowing it first would lose the excess.
            const Type *wide = ct->size(target_) > sizeT->size(target_) ? unsignedVersion(ct) : sizeT;
            ExprPtr cap(new Num(static_cast<long long>(limit)));
            cap->setType(wide);
            ExprPtr over(new Binary(BinOp::Gt, convert(kept(), wide), std::move(cap)));
            over->setType(boolT);
            // Below zero, or below the clauses `new T[n]{...}` lists.
            if (ct->isSigned(target_) || !elements.empty()) {
                ExprPtr least(new Num(static_cast<long long>(elements.size())));
                least->setType(ct);
                ExprPtr below(new Binary(BinOp::Lt, kept(), std::move(least)));
                below->setType(boolT);
                ExprPtr either(new Binary(BinOp::LOr, std::move(below), std::move(over)));
                either->setType(boolT);
                over = std::move(either);
            }
            Signature &f = functions_[(*thrower)[0]];
            markUsed(&f);
            ExprPtr call = completeCall(f.name, f.symbol, nullptr, f.returns, f.params, false, pos,
                                        std::vector<ExprPtr>());
            ExprPtr none(new Num(0LL));
            none->setType(sizeT);
            ExprPtr thrown(new Comma(std::move(call), std::move(none)));
            thrown->setType(sizeT);
            tooMany = std::move(over);
            tooManyThrows = std::move(thrown);
            given = kept();
            mayThrow_++;
        }
        count = std::move(given);
    }
    if (array) {
        ExprPtr n = convert(std::move(count), sizeT);
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
        // The count kept, then asked, and only then the byte count it makes.
        if (tooMany != nullptr) {
            ExprPtr pick(new Conditional(std::move(tooMany), std::move(tooManyThrows), std::move(bytes)));
            pick->setType(sizeT);
            ExprPtr seq(new Comma(std::move(countKept), std::move(pick)));
            seq->setType(sizeT);
            bytes = std::move(seq);
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
    int placementDel = -1;
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
        placementDel = pendingPlacementDelete_;
    } else if (const Signature *own = classAllocator(made, array ? "operatornew[]" : "operatornew")) {
        // [class.free]/2: a class's own `operator new` allocates its objects - and [expr.new]/18
        // asks its access as any member's.
        if (own->access != Access::Public && !insideAccessOf(made, own->access) && !isFriendOf(made))
            src_.fail(pos, "'" + made->describe() + "'s 'operator new" + (array ? "[]" : "") +
                           "' is " + (own->access == Access::Private ? "private" : "protected") +
                           " and this new-expression is outside the class - [expr.new]/18");
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
        mayThrow_++;  // [new.delete.single]/3: may throw bad_alloc, as cl
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
        // The listed elements first, each built where it lives and then guarded, so that one
        // that throws after it destroys it on the way out ([except.ctor]/2); the rest by the loop.
        const Signature *dtor = destructorOf(made);
        std::vector<Temporary> listed;
        ExprPtr built;
        for (std::size_t i = 0; i < elements.size(); i++) {
            ExprPtr one = constructListedElement(made, elements[i],
                                                 elementAddress(baseTemp, baseSlot, made, i), pos);
            if (dtor != nullptr) {
                const int g = guardFlag();
                Temporary t{ baseSlot, made, g };
                t.newElement = true; t.newIndex = static_cast<long long>(i);
                listed.push_back(t);
                ExprPtr armed(new Comma(std::move(one), setGuard(g, 1)));
                armed->setType(types_.intType());
                one = std::move(armed);
            }
            if (built == nullptr) { built = std::move(one); continue; }
            const Type *ot = one->type();
            ExprPtr both(new Comma(std::move(built), std::move(one)));
            both->setType(ot);
            built = std::move(both);
        }
        if (!allListed) {
            ExprPtr n(Var::local(countTemp, countSlot));
            n->setType(sizeT);
            if (!elements.empty()) {
                ExprPtr k(new Num(static_cast<long long>(elements.size())));
                k->setType(sizeT);
                ExprPtr left(new Binary(BinOp::Sub, std::move(n), std::move(k)));
                left->setType(sizeT);
                n = std::move(left);
            }
            ExprPtr rest = callVectorLoop(vectorConstructor(made, pos), made,
                                          elementAddress(baseTemp, baseSlot, made, elements.size()),
                                          std::move(n), pos);
            if (built == nullptr) built = std::move(rest);
            else {
                const Type *rt = rest->type();
                ExprPtr both(new Comma(std::move(built), std::move(rest)));
                both->setType(rt);
                built = std::move(both);
            }
        }
        for (std::size_t i = 0; i < listed.size(); i++) {
            ExprPtr off(new Comma(std::move(built), setGuard(listed[i].flag, 0)));
            off->setType(types_.intType());
            built = std::move(off);
        }
        // [expr.new]/13: a null from the nothrow allocator initialises nothing.
        if (nothrow) built = guardAgainstNull(baseTemp, baseSlot, pointer, std::move(built));
        // [expr.new]/20: an element constructor that throws gives the storage back.
        if ((!reservedPlacement && !userAlloc) || nothrow) {
            const int g = guardFlag();
            Temporary t{ baseSlot, made, g };
            t.newStorage = true; t.newArray = true; t.newNothrow = nothrow; t.newCookie = cookie; t.newGlobal = globalAllocation_;
            statementTemps_.push_back(t);
            ExprPtr armed(new Comma(std::move(keep), setGuard(g, 1)));
            armed->setType(types_.intType());
            keep = std::move(armed);
            ExprPtr done(new Comma(std::move(built), setGuard(g, 0)));
            done->setType(types_.intType());
            built = std::move(done);
        }
        // After the storage, so that unwinding destroys the elements before it gives that back.
        for (std::size_t i = 0; i < listed.size(); i++) statementTemps_.push_back(listed[i]);
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
        // Then the listed elements over the zeroes.
        for (std::size_t i = 0; i < elements.size(); i++) {
            ExprPtr value = listedElementValue(made, elements[i]);
            ExprPtr cell(new Unary('*', elementAddress(temp, slot, made, i)));
            cell->setType(made);
            ExprPtr store(new Assign(std::move(cell), std::move(value)));
            store->setType(made);
            ExprPtr seq(new Comma(std::move(filled), std::move(store)));
            seq->setType(made);
            filled = std::move(seq);
        }
        if (nothrow) filled = guardAgainstNull(temp, slot, pointer, std::move(filled));

        ExprPtr result(Var::local(temp, slot));
        result->setType(pointer);
        const Type *ft = filled->type();
        ExprPtr both(new Comma(std::move(keep), std::move(filled)));
        both->setType(ft);
        ExprPtr all(new Comma(std::move(both), std::move(result)));
        all->setType(pointer);
        return all;
    }

    if (constructed) {
        buildBracedArguments(constructorKey(made->tag()), ctorArgs, pos);
        convertThroughConversionFunction(ctorArgs, made, true, pos);
        // A class whose copy is trivial has no copy constructor to resolve: [class.copy]/12 makes
        // `new (p) T(t)` a copy of the bytes, the store a declaration `T u(t);` already makes.
        if (ctorArgs.size() == 1 && copyConstructorOf(made) == nullptr && moveConstructorOf(made) == nullptr &&
            ctorArgs[0]->type()->unqualified() == made) {
            ExprPtr base(Var::local(temp, slot));
            base->setType(pointer);
            ExprPtr target(new Unary('*', std::move(base)));
            target->setType(made);
            ExprPtr store(new Assign(std::move(target), std::move(ctorArgs[0])));
            store->setType(made);
            if (nothrow) store = guardAgainstNull(temp, slot, pointer, std::move(store));
            ExprPtr made2(new Comma(std::move(keep), std::move(store)));
            made2->setType(made);
            ExprPtr answer(Var::local(temp, slot));
            answer->setType(pointer);
            ExprPtr whole(new Comma(std::move(made2), std::move(answer)));
            whole->setType(pointer);
            return whole;
        }
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
        // **[class.copy]/31: `new K(K(6))` builds the temporary in the storage** - its constructor is given
        // the new object's address, and the copy or move resolution chose above is not called (1.78).
        ExprPtr build;
        Call *inPlace = ctorArgs.size() == 1 && freshTemporarySlot(*ctorArgs[0], made) != 0
                      ? temporaryConstruction(*ctorArgs[0]) : nullptr;
        if (inPlace != nullptr) {
            const int was = freshTemporarySlot(*ctorArgs[0], made);
            ExprPtr self(Var::local(temp, slot));
            self->setType(pointer);
            inPlace->setArg(0, std::move(self));
            adoptTemporary(was);
            build = std::move(ctorArgs[0]);
        } else {
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

            build = completeCall(made->tag(), ctor.symbol, nullptr,
                                 types_.get(Kind::Void), full, false, pos,
                                 std::move(all));
        }
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
        // [expr.new]/20: a constructor that throws gives the storage back - to a placement form's own
        // operator delete where one matches (R27).
        if (!ctor.isNoexcept && ((!reservedPlacement && !userAlloc) || nothrow || placementDel >= 0)) {
            const int g = guardFlag();
            Temporary t{ slot, made, g };
            t.newStorage = true; t.newNothrow = nothrow; t.placementDelete = placementDel; t.newGlobal = globalAllocation_;
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
    struct Global { bool &flag; bool was; ~Global() { flag = was; } } global{ globalAllocation_, globalAllocation_ };
    globalAllocation_ = globalNewDelete_;
    globalNewDelete_ = false;
    bool array = false;
    if (consume("[")) { expect("]"); array = true; }

    // [expr.delete]/1: the operand is a cast-expression - `delete (int *)p` (1.77; it was read as a
    // unary-expression, which refused the cast).
    ExprPtr what = decay(castExpr());
    const Type *t = what->type();
    if (!t->isPointer())
        src_.fail(pos, "'delete' needs a pointer, and this is '" +
                       t->describe() + "'");
    if (t->pointee()->isVoid())
        src_.fail(pos, "'delete' of a 'void *' does not know what it is "
                       "freeing - give it the pointer's real type");

    // **The destructor runs before the memory goes back**, as clang emits it.
    const Signature *dtor = destructorOf(t->pointee());
    // [expr.delete]/7-10 with [class.access]: the destructor and the class's deallocation function are
    // accessible where the delete is written.
    refuseInaccessibleDestructor(t->pointee(), pos, "the object this 'delete' frees");
    if (!globalAllocation_)
        if (const Signature *own = classAllocator(t->pointee(), array ? "operatordelete[]" : "operatordelete")) {
            const Type *cls = t->pointee()->unqualified();
            if (own->access != Access::Public && !insideAccessOf(cls, own->access) && !isFriendOf(cls))
                src_.fail(pos, "'" + cls->describe() + "''s 'operator delete' is " +
                               (own->access == Access::Private ? "private" : "protected") +
                               ", and this 'delete' cannot call it - [expr.delete]/7");
        }

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
    // On Microsoft a destructor entered at a virtual base is reached through that base's table, the
    // object's vbtable saying where it is (M3).
    std::string dispatchTemp = temp;
    int dispatchSlot = slot;
    ExprPtr dispatchSave;
    if (index < 0 && ms)
        for (const MsVftable &tb : msVftables(cls)) {
            if (tb.vbase == nullptr || index >= 0) continue;
            for (std::size_t i = 0; i < tb.slots.size(); i++)
                if (tb.slots[i].name == "~") {
                    index = static_cast<int>(i);
                    ExprPtr obj(Var::local(temp, slot));
                    obj->setType(t);
                    ExprPtr there(new Cast(t, msShifted(convert(std::move(obj), types_.pointerTo(tb.vbase)),
                                                        tb.atInVbase)));
                    there->setType(t);
                    dispatchSlot = allocateFrameSlot(t);
                    dispatchTemp = ".vd" + std::to_string(refTemps_++);
                    ExprPtr held(Var::local(dispatchTemp, dispatchSlot));
                    held->setType(t);
                    dispatchSave.reset(new Assign(std::move(held), std::move(there)));
                    dispatchSave->setType(t);
                    break;
                }
        }
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

    ExprPtr load(Var::local(dispatchTemp, dispatchSlot));
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
    ExprPtr self(Var::local(dispatchTemp, dispatchSlot));
    self->setType(t);
    args.push_back(std::move(self));
    if (ms) {
        ExprPtr flag(new Num(deleting ? 1LL : 0LL));
        flag->setType(flagType);
        args.push_back(std::move(flag));
    }
    ExprPtr call = completeCall("~", std::string(), std::move(entry), ret, full, false,
                                pos, std::move(args));
    if (dispatchSave == nullptr) return call;
    const Type *rt = call->type();
    ExprPtr both(new Comma(std::move(dispatchSave), std::move(call)));
    both->setType(rt);
    return both;
}

// A class's own `operator new` or `operator delete`, else null - looked up
// in the class and then its bases, as [class.free] finds them.
const Parser::Signature *Parser::classAllocator(const Type *made,
                                                const char *which) {
    if (globalAllocation_) return nullptr;
    const Type *cls = made->unqualified();
    const bool freeing = std::string(which).compare(0, 14, "operatordelete") == 0;
    while (cls != nullptr && cls->isStructOrUnion() && !cls->tag().empty()) {
        if (const std::vector<std::size_t> *set = overloadsOf(cls->tag() + "::" + which)) {
            // The usual one beside any placement form, [basic.stc.dynamic.deallocation]/2 - `(void *)`,
            // or `(void *, size_t)`; `(size_t)` to allocate. With none, the first, which the call refuses.
            for (std::size_t k = 0; k < set->size(); k++) {
                const Signature &f = functions_[(*set)[k]];
                if (f.params.size() == 1 ||
                    (freeing && f.params.size() == 2 &&
                     f.params[1]->unqualified() == types_.get(target_.sizeType())))
                    return &f;
            }
            return &functions_[(*set)[0]];
        }
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
    for (const Type *cls = globalAllocation_ ? nullptr : made->unqualified();
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
    // **[expr.new]/20: the placement `operator delete` that matches** - the same parameters after the
    // first, a `void *` - gives the storage back if the constructor throws, with these arguments (R27).
    // They are kept in the frame so that both calls see one evaluation of each.
    pendingPlacementDelete_ = -1;
    const std::string freeKey = key.substr(0, key.size() - std::string(which).size()) +
                                (array ? "operatordelete[]" : "operatordelete");
    bool byValue = !array;
    for (std::size_t i = 1; i < sig.params.size(); i++)
        if (sig.params[i]->isReference()) byValue = false;
    if (byValue && !sig.variadic)
        if (const std::vector<std::size_t> *frees = overloadsOf(freeKey))
            for (std::size_t k = 0; k < frees->size(); k++) {
                const Signature &d = functions_[(*frees)[k]];
                if (d.params.size() != sig.params.size() || d.variadic || !d.params[0]->isPointer() ||
                    !d.params[0]->pointee()->unqualified()->isVoid())
                    continue;
                bool same = true;
                for (std::size_t i = 1; i < d.params.size(); i++)
                    if (d.params[i]->unqualified() != sig.params[i]->unqualified()) same = false;
                if (!same) continue;
                PlacementDelete pd;
                pd.fn = d;
                for (std::size_t i = 1; i < args.size(); i++) {
                    const Type *at = sig.params[i]->unqualified();
                    pd.types.push_back(at);
                    pd.slots.push_back(allocateFrameSlot(at));
                    pd.names.push_back(".newp" + std::to_string(newTemps_++));
                    ExprPtr kept(Var::local(pd.names.back(), pd.slots.back()));
                    kept->setType(at);
                    ExprPtr keep(new Assign(std::move(kept), std::move(args[i])));
                    keep->setType(at);
                    args[i] = std::move(keep);
                }
                markUsed(&functions_[(*frees)[k]]);
                placementDeletes_.push_back(pd);
                pendingPlacementDelete_ = static_cast<int>(placementDeletes_.size() - 1);
                break;
            }
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
    const bool microsoft = target_.microsoftNames();
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
    // [expr.typeid]/3-4: the type, or the operand's, is not an incomplete class.
    if (subject->isStructOrUnion() && !subject->isComplete())
        src_.fail(pos, "'typeid' of '" + subject->describe() + "', an incomplete class - [expr.typeid]/4");
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
    if (glvalue && subject->isStructOrUnion() && subject->polymorphic()) mayThrow_++;
    if (glvalue && subject->isStructOrUnion() && subject->polymorphic() && microsoft) {
        // **The Microsoft runtime reads it**: `__RTtypeid(p)` walks the vftable's
        // locator to the descriptor, and throws bad_typeid for a null - measured from
        // clang -target x86_64-pc-windows-msvc, one argument.
        const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
        ExprPtr at(new Unary('&', std::move(operand)));
        at->setType(types_.pointerTo(subject));
        std::vector<ExprPtr> args;
        args.push_back(convert(msToVfptr(std::move(at), subject), voidPtr));
        ExprPtr got = runtimeCall("__RTtypeid", voidPtr, std::move(args));
        address.reset(new Cast(infoPtr, std::move(got)));
        address->setType(infoPtr);
    } else if (microsoft) {
        // The descriptor, emitted beside the throw records; a class with a
        // vftable already has one and the emitter writes it once.
        MicrosoftThrow names;
        std::string why;
        if (!microsoftTypeidNames(subject, &names, &why))
            src_.fail(pos, "'typeid' cannot name the type of this: " + why);
        finishMicrosoftThrow(names, false);
        Var *ti = Var::global(names.descriptor);
        ti->setSymbol(names.descriptor);
        ExprPtr ref(ti);
        ref->setType(types_.get(Kind::Char));
        ExprPtr addr(new Unary('&', std::move(ref)));
        addr->setType(types_.pointerTo(types_.get(Kind::Char)));
        address.reset(new Cast(infoPtr, std::move(addr)));
        address->setType(infoPtr);
    } else if (glvalue && subject->isStructOrUnion() && subject->polymorphic()) {
        // The object's first word is the vptr; the type_info pointer sits
        // one word below the address point it holds.
        const Type *chars = types_.pointerTo(types_.get(Kind::Char));
        const Unary *star = dynamic_cast<const Unary *>(operand.get());
        const bool throwsOnNull = star != nullptr && star->op() == '*' && target_.hasBadTypeid();
        ExprPtr at(new Unary('&', std::move(operand)));
        at->setType(types_.pointerTo(subject));
        if (throwsOnNull) {
            // [expr.typeid]/2: the pointer of `*p` is tested, a null one being `__cxa_bad_typeid()`,
            // the ABI's routine that throws std::bad_typeid, as clang calls.
            const Type *ptr = types_.pointerTo(subject);
            const int slot = allocateFrameSlot(ptr);
            const std::string temp = ".typeidptr" + std::to_string(newTemps_++);
            ExprPtr held(Var::local(temp, slot));
            held->setType(ptr);
            ExprPtr save(new Assign(std::move(held), std::move(at)));
            save->setType(ptr);
            ExprPtr test(Var::local(temp, slot));
            test->setType(ptr);
            ExprPtr one(new Num(1LL));
            one->setType(types_.intType());
            ExprPtr fail = runtimeCall("__cxa_bad_typeid", types_.get(Kind::Void), std::vector<ExprPtr>());
            ExprPtr alsoOne(new Num(1LL));
            alsoOne->setType(types_.intType());
            ExprPtr failed(new Comma(std::move(fail), std::move(alsoOne)));
            failed->setType(types_.intType());
            ExprPtr check(new Conditional(std::move(test), std::move(one), std::move(failed)));
            check->setType(types_.intType());
            ExprPtr first(new Comma(std::move(save), std::move(check)));
            first->setType(types_.intType());
            ExprPtr again(Var::local(temp, slot));
            again->setType(ptr);
            at.reset(new Comma(std::move(first), std::move(again)));
            at->setType(ptr);
        }
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
