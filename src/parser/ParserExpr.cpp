// The parser: expressions, up to the operator ladder. Pointer arithmetic and the
// usual conversions, the named casts, the primary expression and the names it
// looks up, `decltype`, reference binding, and the postfix and unary levels.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Operator.h"
#include "../Source.h"

#include <climits>
#include <limits>
#include <cstring>

// The pointer-width integer an offset is computed in: `long` where it is as wide as a pointer, else
// `long long` (x86_64-windows), so an index is scaled once at pointer width - and never wider, which on
// the C6000's four-byte pointer would make every index 64-bit arithmetic.
// **It is the type <cstddef> calls ptrdiff_t** ([expr.add]/6: the difference of two pointers is of that type, and
// `decltype(q - p)` shows which): `int` where an int is as wide as a pointer (the C6000, as TI's headers have it),
// `long` where a long is (Linux, macOS), `long long` on x86_64-windows. Until 1.68 the C6000 got `long` - as wide,
// another type, and another letter in a mangled name than the header's own.
const Type *Parser::ptrdiffType() {
    const int pointer = target_.sizeOf(Kind::Pointer);
    if (target_.sizeOf(Kind::Int) == pointer) return types_.get(Kind::Int);
    return target_.sizeOf(Kind::Long) == pointer ? types_.get(Kind::Long) : types_.get(Kind::LongLong);
}

ExprPtr Parser::pointerAdd(ExprPtr p, ExprPtr n) {
    const Type *pt = p->type();
    long long stride = pt->pointee()->size(target_);
    const Type *pd = ptrdiffType();

    ExprPtr size(new Num(stride));
    size->setType(pd);
    ExprPtr scaled(new Binary(BinOp::Mul, convert(std::move(n), pd), std::move(size)));
    scaled->setType(pd);

    ExprPtr sum(new Binary(BinOp::Add, std::move(p), std::move(scaled)));
    sum->setType(pt);
    return sum;
}

// [expr.add]/6: the pointees are cv-qualified or not versions of one type, so `char *` minus
// `const char *` is a difference; the qualifiers are compared away, the types are not.
ExprPtr Parser::pointerSub(ExprPtr l, ExprPtr r, std::size_t pos) {
    if (l->type()->pointee()->unqualified() != r->type()->pointee()->unqualified())
        src_.fail(pos, "'" + l->type()->describe() + "' minus '" +
                       r->type()->describe() + "' needs the same pointee type");
    long long stride = l->type()->pointee()->size(target_);

    const Type *pd = ptrdiffType();
    ExprPtr diff(new Binary(BinOp::Sub, std::move(l), std::move(r)));
    diff->setType(pd);
    ExprPtr size(new Num(stride));
    size->setType(pd);
    ExprPtr n(new Binary(BinOp::Div, std::move(diff), std::move(size)));
    n->setType(pd);
    return n;
}

// What a BinOp is written as, which is what `operator` is followed by when
// somebody overloads it. The backends spell these too, for their own output;
// this is the source spelling and belongs to the front end.
const char *binOpSpelling(BinOp op) {
    switch (op) {
    case BinOp::Add:    return "+";
    case BinOp::Sub:    return "-";
    case BinOp::Mul:    return "*";
    case BinOp::Div:    return "/";
    case BinOp::Mod:    return "%";
    case BinOp::Shl:    return "<<";
    case BinOp::Shr:    return ">>";
    case BinOp::BitAnd: return "&";
    case BinOp::BitOr:  return "|";
    case BinOp::BitXor: return "^";
    case BinOp::Eq:     return "==";
    case BinOp::Ne:     return "!=";
    case BinOp::Lt:     return "<";
    case BinOp::Le:     return "<=";
    case BinOp::Gt:     return ">";
    case BinOp::Ge:     return ">=";
    case BinOp::LAnd:   return "&&";
    case BinOp::LOr:    return "||";
    }
    return "";
}

// [expr.unary.op]/3 through a template-id: the qualifier read as a type from a
// copy of its tokens, the member's name left where it is.
const Type *Parser::templateIdQualifier(std::size_t *member) {
    std::size_t i = at_;
    if (tokens_[i].is("::")) i++;
    bool angle = false;
    std::size_t lastColon = 0;
    std::string written;
    for (;;) {
        if (tokens_[i].kind != TokenKind::Ident) return nullptr;
        written += (written.empty() ? "" : "::") + tokens_[i].text;
        i++;
        if (tokens_[i].is("<")) {
            // `&a < b > ::c` with `a` an object is two comparisons.
            if (!isClassTemplate(written)) return nullptr;
            angle = true;
            int depth = 0;
            for (; i < tokens_.size(); i++) {
                if (depth > 0 && (tokens_[i].is("(") || tokens_[i].is("["))) { i = groupCloseAt(i); continue; }
                if (tokens_[i].is("<")) depth++;
                else if (tokens_[i].is(">")) depth--;
                else if (tokens_[i].is(">>")) depth -= 2;
                else if (tokens_[i].is(";") || tokens_[i].is("{") || tokens_[i].kind == TokenKind::End) return nullptr;
                if (depth <= 0) break;
            }
            if (depth != 0) return nullptr;
            i++;
        }
        if (!tokens_[i].is("::")) return nullptr;
        lastColon = i++;
        if (tokens_[i].kind == TokenKind::Ident && !tokens_[i + 1].is("::") && !tokens_[i + 1].is("<")) break;
    }
    if (!angle) return nullptr;
    const std::size_t resume = at_;
    const std::size_t copy = tokens_.size();
    for (std::size_t k = at_; k < lastColon; k++) tokens_.push_back(tokens_[k]);
    Token stop;
    stop.kind = TokenKind::End;
    stop.pos = tokens_[lastColon].pos;
    tokens_.push_back(stop);
    at_ = copy;
    StorageClass sc;
    const Type *t = specifiers(&sc);
    const bool whole = peek().kind == TokenKind::End;
    tokens_.resize(copy);
    at_ = resume;
    if (!whole || t == nullptr || !t->unqualified()->isStructOrUnion()) return nullptr;
    *member = lastColon + 1 - at_;
    return t->unqualified();
}

ExprPtr Parser::overloadedBinary(BinOp op, ExprPtr &lhs, ExprPtr &rhs,
                                 std::size_t pos) {
    const Type *lt = lhs->type();
    const Type *rt = rhs->type();
    // **An enumeration operand asks for an operator too** -
    // [over.match.oper]/1,3: `N::operator|(Flag, Flag)` by ADL, else the
    // built-in; a member function pointer is no class ([expr.eq]/2).
    const bool leftIsClass = lt->unqualified()->isStructOrUnion() &&
                             !lt->unqualified()->isMemberFunctionPointer();
    const bool rightIsClass = rt->unqualified()->isStructOrUnion() &&
                              !rt->unqualified()->isMemberFunctionPointer();
    if (!leftIsClass && !rightIsClass &&
        !lt->unqualified()->isEnumeration() && !rt->unqualified()->isEnumeration())
        return nullptr;

    const char *spelling = binOpSpelling(op);
    const std::string name = std::string("operator") + spelling;

    // The member and non-member candidates are ranked *together*, which is what
    // [over.match.oper] asks for and what asking the two questions in order got
    // wrong: taking the member first accepts an ambiguity clang refuses.
    switch (resolveOperator(name, *lhs, rhs.get(), pos)) {
    case OperatorChoice::Member: {
        std::vector<ExprPtr> args;
        args.push_back(std::move(rhs));
        return memberCallWith(std::move(lhs), lt, name, pos, std::move(args));
    }
    case OperatorChoice::NonMember: {
        std::vector<ExprPtr> args;
        args.push_back(std::move(lhs));
        args.push_back(std::move(rhs));
        const Signature &sig = resolveChosenOperator(name, args, pos);
        return completeCall(name, sig.symbol, nullptr, sig.returns, sig.params,
                            sig.variadic, pos, std::move(args),
                            !sig.owner.empty());
    }
    case OperatorChoice::None:
        break;
    }
    // Two enumerations (or one and a number) with no operator of their own: the built-in's.
    if (!leftIsClass && !rightIsClass) return nullptr;

    // **No operator took them, so the built-in one is offered a class that can
    // become a number.**
    {
        ExprPtr l = std::move(lhs);
        ExprPtr r = std::move(rhs);
        bool moved = false;
        if (leftIsClass && soleNumericConversion(lt) != nullptr) {
            const Type *object = l->type();
            std::vector<ExprPtr> none;
            l = memberCallWith(std::move(l), object,
                               soleNumericConversion(lt)->name, pos,
                               std::move(none));
            moved = true;
        }
        if (rightIsClass && soleNumericConversion(rt) != nullptr) {
            const Type *object = r->type();
            std::vector<ExprPtr> none;
            r = memberCallWith(std::move(r), object,
                               soleNumericConversion(rt)->name, pos,
                               std::move(none));
            moved = true;
        }
        const bool compares = op == BinOp::Eq || op == BinOp::Ne || op == BinOp::Lt || op == BinOp::Le ||
                              op == BinOp::Gt || op == BinOp::Ge;
        // a comparison goes back to its own rules with what the classes became: two pointers, a pointer and
        // `nullptr` (1.80: `p == nullptr` of a class that converts to a pointer was "cannot be combined")
        if (moved && compares && !l->type()->unqualified()->isStructOrUnion() &&
            !r->type()->unqualified()->isStructOrUnion()) {
            lhs = std::move(l);
            rhs = std::move(r);
            return nullptr;
        }
        if (moved && !l->type()->unqualified()->isStructOrUnion() &&
            !r->type()->unqualified()->isStructOrUnion())
            return arithmetic(op, std::move(l), std::move(r), pos);
        lhs = std::move(l);
        rhs = std::move(r);
    }

    src_.fail(pos, "'" + lt->describe() + "' and '" + rt->describe() +
                   "' cannot be combined with '" + spelling + "' - one of them "
                   "is a class, and no '" + name + "' is declared that takes "
                   "them");
}

// `a && b`, `a || b`, `a, b`, `a ->* b` with a class or enum operand: a call
// where an operator function takes them ([over.match.oper]/1, both evaluated),
// else the built-in. Null for the built-in, the operands untouched.
ExprPtr Parser::overloadedSpelled(const char *spelling, ExprPtr &lhs, ExprPtr &rhs, std::size_t pos) {
    const Type *lt = lhs->type(), *rt = rhs->type();
    if (lt == nullptr || rt == nullptr || lt->isVoid() || rt->isVoid()) return nullptr;
    auto operand = [](const Type *t) {
        const Type *u = t->unqualified();
        return (u->isStructOrUnion() && !u->isMemberFunctionPointer()) || u->isEnumeration();
    };
    if (!operand(lt) && !operand(rt)) return nullptr;
    const std::string name = std::string("operator") + spelling;
    switch (resolveOperator(name, *lhs, rhs.get(), pos)) {
    case OperatorChoice::Member: {
        std::vector<ExprPtr> args;
        args.push_back(std::move(rhs));
        return memberCallWith(std::move(lhs), lt, name, pos, std::move(args));
    }
    case OperatorChoice::NonMember: {
        std::vector<ExprPtr> args;
        args.push_back(std::move(lhs));
        args.push_back(std::move(rhs));
        const Signature &sig = resolveChosenOperator(name, args, pos);
        return completeCall(name, sig.symbol, nullptr, sig.returns, sig.params,
                            sig.variadic, pos, std::move(args), !sig.owner.empty());
    }
    case OperatorChoice::None:
        break;
    }
    return nullptr;
}

// `-v`, `!v`, `~v`, `*v`, `&v` and the two increments - the same merged candidate
// set as a binary operator, with one operand. **Null means "carry on with the
// built-in", not "there is a problem"**: a class with no `operator&` has an address.
ExprPtr Parser::overloadedUnary(const char *spelling, ExprPtr &operand,
                                std::size_t pos) {
    if (!operand->type()->unqualified()->isStructOrUnion()) return nullptr;

    // Read before the operand is moved from: memberCallWith wants the type
    // with its constness still on it, to rank the implicit object parameter.
    const Type *self = operand->type();
    const std::string name = std::string("operator") + spelling;
    switch (resolveOperator(name, *operand, nullptr, pos)) {
    case OperatorChoice::Member: {
        std::vector<ExprPtr> args;
        return memberCallWith(std::move(operand), self, name, pos,
                              std::move(args));
    }
    case OperatorChoice::NonMember: {
        std::vector<ExprPtr> args;
        args.push_back(std::move(operand));
        const Signature &sig = resolveChosenOperator(name, args, pos);
        return completeCall(name, sig.symbol, nullptr, sig.returns, sig.params,
                            sig.variadic, pos, std::move(args),
                            !sig.owner.empty());
    }
    case OperatorChoice::None:
        break;
    }
    return nullptr;
}

ExprPtr Parser::arithmetic(BinOp op, ExprPtr lhs, ExprPtr rhs, std::size_t pos) {
    // Before decay, because decay is a rule about built-in operands and a
    // class is not one.
    if (ExprPtr call = overloadedBinary(op, lhs, rhs, pos)) return call;
    lhs = decay(std::move(lhs));
    rhs = decay(std::move(rhs));
    refuseScopedOperand(lhs->type(), rhs->type(), binOpSpelling(op), pos);

    if (op == BinOp::Add) {
        if (lhs->type()->isPointer() && rhs->type()->isInteger())
            return pointerAdd(std::move(lhs), std::move(rhs));
        if (lhs->type()->isInteger() && rhs->type()->isPointer())
            return pointerAdd(std::move(rhs), std::move(lhs));
    }
    if (op == BinOp::Sub && lhs->type()->isPointer()) {
        if (rhs->type()->isInteger()) {
            // **The index is widened to the pointer's width before it is
            // negated** ([expr.add]/4): negated narrow, `p - n` for an
            // unsigned n was p + (2^32 - n); as wide (the C6000), as it was.
            const Type *lt = promote(rhs->type());
            const Type *pd = ptrdiffType();
            if (lt->size(target_) < pd->size(target_)) lt = pd;
            ExprPtr neg(new Unary('-', convert(std::move(rhs), lt)));
            neg->setType(lt);
            return pointerAdd(std::move(lhs), std::move(neg));
        }
        if (rhs->type()->isPointer())
            return pointerSub(std::move(lhs), std::move(rhs), pos);
    }

    if (!lhs->type()->isArithmetic() || !rhs->type()->isArithmetic())
        src_.fail(pos, "'" + lhs->type()->describe() + "' and '" +
                       rhs->type()->describe() + "' cannot be combined like that");
    if (op == BinOp::Mod && (lhs->type()->isFloating() || rhs->type()->isFloating()))
        src_.fail(pos, "'%' needs integers, not floating point");

    const Type *common = usualArithmetic(lhs->type(), rhs->type());
    ExprPtr n(new Binary(op, convert(std::move(lhs), common),
                             convert(std::move(rhs), common)));
    n->setType(common);
    return n;
}

ExprPtr Parser::comparison(BinOp op, ExprPtr lhs, ExprPtr rhs, std::size_t pos) {
    if (ExprPtr call = overloadedBinary(op, lhs, rhs, pos)) return call;
    lhs = decay(std::move(lhs));
    rhs = decay(std::move(rhs));
    ExprPtr n;
    // **`nullptr` may be compared for equality and for nothing else.** [expr.rel]
    // wants two pointers and std::nullptr_t is not one, so `p < nullptr` is a
    // diagnostic; two nullptrs are always equal, the type having one value.
    const bool null = lhs->type()->isNullPtr() || rhs->type()->isNullPtr();
    if (null && op != BinOp::Eq && op != BinOp::Ne)
        src_.fail(pos, "'std::nullptr_t' has one value, so there is nothing to "
                       "order - only '==' and '!=' are written with 'nullptr'");
    // A pointer to a member function compares as a pointer to a member does, [expr.eq]/2 (1.27,
    // 2026-10-21, review of 1.25, item 3: `mf == nullptr` was refused as mismatched types).
    auto toMember = [](const Type *t) { return t->isMemberPointer() || t->isMemberFunctionPointer(); };
    if (null || lhs->type()->isPointer() || rhs->type()->isPointer() ||
        toMember(lhs->type()) || toMember(rhs->type())) {
        // **A pointer beside an integer: only a null pointer constant** -
        // [expr.eq], [expr.rel]; `p == 1` was accepted (cl C2446, review A26).
        for (int side = 0; side < 2; side++) {
            const Expr &ptr = side == 0 ? *lhs : *rhs;
            const Expr &other = side == 0 ? *rhs : *lhs;
            if ((ptr.type()->isPointer() || toMember(ptr.type())) &&
                other.type()->isArithmetic() && !isNullConstant(other))
                src_.fail(pos, "'" + ptr.type()->describe() + "' is compared with '" +
                               other.type()->describe() + "' - a pointer compares with "
                               "a pointer, or with 0; a cast says you meant the integer");
        }
        // The null a data member pointer holds is -1, so nullptr - or the constant 0 - becomes that first.
        auto nullish = [](const Expr &e) { return e.type()->isNullPtr() || (e.type()->isInteger() && isNullConstant(e)); };
        if (nullish(*lhs) && toMember(rhs->type())) lhs = convert(std::move(lhs), rhs->type());
        if (nullish(*rhs) && toMember(lhs->type())) rhs = convert(std::move(rhs), lhs->type());
        // Two pointers to member functions are equal when they name one function: the code word of
        // each pair is compared, and a null one has none (1.27, 2026-10-21, review of 1.25, item 3).
        if (lhs->type()->isMemberFunctionPointer() && rhs->type()->isMemberFunctionPointer()) {
            auto codeWord = [&](ExprPtr e) {
                const Member *fn = e->type()->unqualified()->findMember("$fn");
                ExprPtr word(new MemberAccess(std::move(e), "$fn", fn->offset, 0, 0));
                word->setType(fn->type);
                return word;
            };
            lhs = codeWord(std::move(lhs));
            rhs = codeWord(std::move(rhs));
        }
        n = ExprPtr(new Binary(op, std::move(lhs), std::move(rhs)));
    } else {
        const Type *common = usualArithmetic(lhs->type(), rhs->type());
        n = ExprPtr(new Binary(op, convert(std::move(lhs), common),
                                   convert(std::move(rhs), common)));
    }
    // **[expr.rel]/1 and [expr.eq]/1: the result is `bool`.** It had been `int`,
    // which is C's answer, and the difference shows in `sizeof(a == b)`, in
    // overloading bool against int, and in `auto`. A bool promotes to int anyway.
    n->setType(types_.get(Kind::Bool));
    return n;
}

// `static_cast<T>(e)`, parsed here because [expr.post] makes it a postfix-expression
// - `static_cast<B &>(d).f()`. **The reference case is what it was written for**:
// `static_cast<T &&>` is what std::move is. Every other type goes to convert().
ExprPtr Parser::staticCast(std::size_t pos) {
    expect("<");
    StorageClass sc;
    const Type *to = specifiers(&sc);
    to = declarator(to, true).type;
    if (!atClosingAngle())
        src_.fail(peek().pos, "expected '>' to close 'static_cast<'");
    takeClosingAngle();
    expect("(");
    ExprPtr v;
    { OverloadTarget hold(*this, to); v = expr(); }
    expect(")");
    // [class.abstract]/3: no object of an abstract class is made, a cast's result included.
    if (to->unqualified()->isStructOrUnion())
        checkNotAbstract(to->unqualified(), pos, "the result of this 'static_cast'");

    if (to->isReference()) {
        const Type *referent = to->referent();
        v = useReference(std::move(v));
        if (!isGlvalue(*v))
            src_.fail(pos, "'static_cast<" + to->describe() + ">' needs an "
                           "object to cast, and this is a value with no "
                           "address of its own - it is already the kind of "
                           "thing a '" + to->describe() + "' binds to");
        // **[expr.static.cast]/2: a base lvalue cast to a reference to a derived class** -
        // `&v` walked back by the base's offset and dereferenced, the const kept.
        if (v->type()->unqualified() != referent->unqualified() &&
            v->type()->unqualified()->isStructOrUnion() && referent->unqualified()->isStructOrUnion()) {
            bool viaVirtual = false;
            const int off = downcastOffset(referent, v->type(), &viaVirtual);
            if (off >= 0 || viaVirtual) {
                if (viaVirtual)
                    src_.fail(pos, "'static_cast<" + to->describe() + ">' of a '" + v->type()->describe() +
                                   "' - the base is a virtual base, whose offset is not a constant; "
                                   "'dynamic_cast' is the cast that asks the object");
                if (v->type()->isConst() && !referent->isConst())
                    src_.fail(pos, "'static_cast<" + to->describe() + ">' of a '" +
                                   v->type()->describe() + "' - a cast does not take const off");
                const Type *dst = v->type()->isConst() ? types_.withConst(referent->unqualified())
                                                       : referent->unqualified();
                const Type *srcPtr = types_.pointerTo(v->type());
                ExprPtr addr(new Unary('&', std::move(v)));
                addr->setType(srcPtr);
                ExprPtr moved = downcastPointer(std::move(addr), types_.pointerTo(dst), off);
                ExprPtr obj(new Unary('*', std::move(moved)));
                obj->setType(dst);
                if (to->isRValueReference()) obj->setXvalue();
                return obj;
            }
        }
        // **[expr.static.cast]/2-3: a derived lvalue cast to a reference to its base** - the
        // derived-to-base conversion `B &b = d;` makes, written as a cast (R2: refused before).
        if (v->type()->unqualified() != referent->unqualified() &&
            v->type()->unqualified()->isStructOrUnion() && referent->unqualified()->isStructOrUnion() &&
            accessibleBaseOffset(v->type()->unqualified(), referent->unqualified()) >= 0) {
            if (v->type()->isConst() && !referent->isConst())
                src_.fail(pos, "'static_cast<" + to->describe() + ">' of a '" +
                               v->type()->describe() + "' - a cast does not take const off");
            const Type *dst = v->type()->isConst() || referent->isConst()
                            ? types_.withConst(referent->unqualified()) : referent->unqualified();
            const Type *srcPtr = types_.pointerTo(v->type());
            ExprPtr addr(new Unary('&', std::move(v)));
            addr->setType(srcPtr);
            ExprPtr obj(new Unary('*', convert(std::move(addr), types_.pointerTo(dst))));
            obj->setType(dst);
            if (to->isRValueReference()) obj->setXvalue();
            return obj;
        }
        if (v->type()->unqualified() != referent->unqualified())
            src_.fail(pos, "'static_cast<" + to->describe() + ">' of a '" +
                           v->type()->describe() + "' - casting a reference "
                           "to a different type is not supported yet, and "
                           "this one names '" + referent->describe() + "'");
        if ((v->type()->isConst() && !referent->isConst()) || (v->type()->isVolatile() && !referent->isVolatile()))
            src_.fail(pos, "'static_cast<" + to->describe() + ">' of a '" +
                           v->type()->describe() + "' - a cast does not take "
                           "const or volatile off; 'const_cast' is what does, and the two "
                           "are written separately on purpose");
        // An lvalue reference cast leaves an lvalue and there is nothing to
        // mark; an rvalue reference cast is the whole point of this function.
        if (to->isRValueReference()) v->setXvalue();
        v->setDeclCategory(Expr::Category::Natural);   // a reference cast names the object
        return v;
    }

    // A cast to a non-reference type is a prvalue, whatever node the conversion hands back.
    ExprPtr r = [&]() -> ExprPtr {
        v = decay(std::move(v));
        if (to->isVoid()) {
            ExprPtr c(new Cast(to, std::move(v)));
            return c;
        }
        // **[expr.static.cast]/11: `B *` to `D *` where B is a base of D** - the inverse of the
        // conversion `convert` makes the other way, refused through a virtual base.
        if (to->isPointer() && v->type()->isPointer() && to->pointee()->unqualified()->isStructOrUnion() &&
            v->type()->pointee()->unqualified()->isStructOrUnion() &&
            to->pointee()->unqualified() != v->type()->pointee()->unqualified()) {
            bool viaVirtual = false;
            const int off = downcastOffset(to->pointee(), v->type()->pointee(), &viaVirtual);
            if (viaVirtual)
                src_.fail(pos, "'static_cast<" + to->describe() + ">' of a '" + v->type()->describe() +
                               "' - the base is a virtual base, whose offset is not a constant; "
                               "'dynamic_cast' is the cast that asks the object");
            if (off >= 0) {
                if (v->type()->pointee()->isConst() && !to->pointee()->isConst())
                    src_.fail(pos, "'static_cast<" + to->describe() + ">' of a '" +
                                   v->type()->describe() + "' - a cast does not take const off");
                return downcastPointer(std::move(v), to, off);
            }
        }
        refuseUnrelatedClassCast(*v, to, pos, "'static_cast'");
        // [expr.static.cast]: between object pointers only `void *` either
        // way or along a base-derived line; `static_cast<int *>(&someDouble)`
        // is a pun, reinterpret_cast's, and is refused.
        if (to->isPointer() && v->type()->isPointer()) {
            const Type *tp = to->pointee()->unqualified(), *vp = v->type()->pointee()->unqualified();
            if (tp != vp && !tp->isVoid() && !vp->isVoid() && !tp->isFunction() && !vp->isFunction() &&
                !(tp->isStructOrUnion() && vp->isStructOrUnion()))
                src_.fail(pos, "'static_cast<" + to->describe() + ">' of a '" + v->type()->describe() +
                               "' - the pointees are unrelated types, and static_cast converts a "
                               "pointer only to or from 'void *' or along a base-derived line; "
                               "reinterpret_cast is the cast that reads the bytes as another type");
            refuseCastingAwayConst(v->type(), to, pos, "static_cast");
        }
        if (ExprPtr copy = castToOwnClass(v, to, pos)) return copy;
        return convert(std::move(v), to, true);
    }();
    r->setDeclCategory(Expr::Category::Prvalue);
    return r;
}

// **[expr.static.cast]/1 and [expr.reinterpret.cast]/2: neither casts away constness**
// ([expr.const.cast]/8) - at any level of the pointer, a const in the source the target drops.
// `static_cast<int *>(&constInt)` and its reinterpret_cast were accepted (review of 1.30, A1).
void Parser::refuseCastingAwayConst(const Type *from, const Type *to, std::size_t pos,
                                    const char *cast) const {
    for (const Type *f = from, *t = to; f != nullptr && t != nullptr && f->isPointer() && t->isPointer();
         f = f->pointee(), t = t->pointee())
        if ((f->pointee()->isConst() && !t->pointee()->isConst()) ||
            (f->pointee()->isVolatile() && !t->pointee()->isVolatile()))
            src_.fail(pos, std::string("'") + cast + "<" + to->describe() + ">' of a '" + from->describe() +
                           "' - a cast does not take const or volatile off; 'const_cast' is what does, "
                           "and the two are written separately on purpose");
}

int Parser::downcastOffset(const Type *derived, const Type *base, bool *viaVirtual) const {
    if (derived == nullptr || base == nullptr) return -1;
    const Type *d = derived->unqualified();
    if (d == base->unqualified()) return 0;
    const std::vector<Type::BaseSpec> &bases = d->bases();
    for (std::size_t i = 0; i < bases.size(); i++) {
        if (!baseStepAccessible(d, bases[i].access)) continue;
        const int deeper = downcastOffset(bases[i].type, base, viaVirtual);
        if (deeper < 0) continue;
        if (bases[i].isVirtual) { *viaVirtual = true; return -1; }
        return bases[i].offset + deeper;
    }
    return -1;
}

// At offset 0 only the type changes; otherwise the pointer is held, tested and moved back -
// `(char *)0 - 4` is not null, so the test is the rule and not caution.
ExprPtr Parser::downcastPointer(ExprPtr p, const Type *to, int off) {
    if (off == 0) {
        ExprPtr c(new Cast(to, std::move(p)));
        c->setType(to);
        return c;
    }
    const Type *chars = types_.pointerTo(types_.get(Kind::Char));
    ExprPtr asChars(new Cast(chars, std::move(p)));
    asChars->setType(chars);
    const int slot = allocateFrameSlot(chars);
    const std::string temp = ".dp" + std::to_string(refTemps_++);
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
    ExprPtr moved(new Binary(BinOp::Sub, std::move(shift), std::move(by)));
    moved->setType(chars);
    ExprPtr zero(new Num(0LL));
    zero->setType(chars);
    ExprPtr pick(new Conditional(std::move(test), std::move(moved), std::move(zero)));
    pick->setType(chars);
    ExprPtr both(new Comma(std::move(save), std::move(pick)));
    both->setType(chars);
    ExprPtr out(new Cast(to, std::move(both)));
    out->setType(to);
    return out;
}


// **Two types that differ only in cv-qualifiers, at every level** - what
// [expr.const.cast] calls "similar": strip the pointers in lockstep and the two must
// arrive at the same type. Only `const` is a qualifier this compiler has.
static bool differOnlyInQualifiers(const Type *a, const Type *b) {
    for (;;) {
        a = a->unqualified();
        b = b->unqualified();
        if (a == b) return true;
        if (!a->isPointer() || !b->isPointer()) return false;
        a = a->pointee();
        b = b->pointee();
    }
}

// `const_cast<T>(e)` - the only cast that may take const off, and the only thing it
// may do. It generates nothing: the value is unchanged and what moves is the type,
// which is why C++ made it a cast of its own rather than letting the C one do it.
ExprPtr Parser::constCast(std::size_t pos) {
    expect("<");
    StorageClass sc;
    const Type *to = specifiers(&sc);
    to = declarator(to, true).type;
    if (!atClosingAngle())
        src_.fail(peek().pos, "expected '>' to close 'const_cast<'");
    takeClosingAngle();
    expect("(");
    ExprPtr v = expr();
    expect(")");

    if (to->isReference()) {
        v = useReference(std::move(v));
        if (!isGlvalue(*v))
            src_.fail(pos, "'const_cast<" + to->describe() + ">' needs an "
                           "object, and this is a value with no address of its "
                           "own - there is no const on it to take off");
        if (!differOnlyInQualifiers(v->type(), to->referent()))
            src_.fail(pos, "'const_cast<" + to->describe() + ">' of a '" +
                           v->type()->describe() + "' - const_cast changes "
                           "const and volatile and nothing else, and these two "
                           "are different types");
        v->setType(to->referent());
        return v;
    }

    v = decay(std::move(v));
    if (!to->isPointer())
        src_.fail(pos, "'const_cast<" + to->describe() + ">' - const_cast is "
                       "written on a pointer or a reference, which are the "
                       "things that carry a const somebody might want off. A "
                       "value is copied, so its own const never stood in the "
                       "way");
    if (!v->type()->isPointer())
        src_.fail(pos, "'const_cast<" + to->describe() + ">' of a '" +
                       v->type()->describe() + "', which is not a pointer");
    if (!differOnlyInQualifiers(v->type(), to))
        src_.fail(pos, "'const_cast<" + to->describe() + ">' of a '" +
                       v->type()->describe() + "' - const_cast changes const "
                       "and volatile and nothing else, and these two point at "
                       "different types");

    ExprPtr c(new Cast(to, std::move(v)));
    c->setType(to);
    return c;
}

// `reinterpret_cast<T>(e)` - "read these bits as something else", generating nothing
// on any target here, every conversion it allows being between things of one size.
// **It may not take const off**, which is the line between it and const_cast.
ExprPtr Parser::reinterpretCast(std::size_t pos) {
    expect("<");
    StorageClass sc;
    const Type *to = specifiers(&sc);
    to = declarator(to, true).type;
    if (!atClosingAngle())
        src_.fail(peek().pos, "expected '>' to close 'reinterpret_cast<'");
    takeClosingAngle();
    expect("(");
    ExprPtr v = expr();
    expect(")");

    // [expr.const.cast]/8 from the pointee down: no shared level drops a const, and one
    // that adds a const has const at every level above it, as a qualification conversion
    // would. A pointer's own top level is its value's and is never asked about.
    auto castsAwayConst = [&](const Type *from, const Type *want) -> const char * {
        for (bool constAbove = true;;) {
            if (from->isConst() && !want->isConst())
                return "would take the const off - 'const_cast' is what does that, and the "
                       "two are written separately on purpose";
            if (want->isConst() && !from->isConst() && !constAbove)
                return "adds a const beneath a level with none, which is a way to take one "
                       "off - it is refused for the reason 'char **' does not become "
                       "'const char **'";
            constAbove = constAbove && want->isConst();
            from = from->unqualified();
            want = want->unqualified();
            if (!from->isPointer() || !want->isPointer()) return nullptr;
            from = from->pointee();
            want = want->pointee();
        }
    };
    auto refuseIf = [&](const Type *was, const char *why) {
        if (why)
            src_.fail(pos, "'reinterpret_cast<" + to->describe() + ">' of a '" +
                           was->describe() + "' " + why);
    };

    // A reference reinterpretation is the same bits under another name - the
    // object is not moved and its address is not changed, so the operand comes
    // back with a new type and nothing else.
    if (to->isReference()) {
        v = useReference(std::move(v));
        if (!isGlvalue(*v))
            src_.fail(pos, "'reinterpret_cast<" + to->describe() + ">' needs "
                           "an object to reinterpret, and this is a value with "
                           "no address of its own");
        refuseIf(v->type(), castsAwayConst(v->type(), to->referent()));
        v->setType(to->referent());
        return v;
    }

    v = decay(std::move(v));
    const Type *from = v->type();

    const bool fromPointer = from->isPointer() || from->isNullPtr();
    if (to->isPointer() && fromPointer) {
        if (from->isPointer())
            refuseIf(from, castsAwayConst(from->unqualified()->pointee(),
                                          to->unqualified()->pointee()));
    } else if (to->isInteger() && fromPointer) {
        // Measured: clang refuses a cast to an integer too small to hold the
        // pointer rather than truncating it quietly.
        if (to->size(target_) < from->size(target_))
            src_.fail(pos, "'reinterpret_cast<" + to->describe() + ">' of a '" +
                           from->describe() + "' loses part of the address - "
                           "the type has to be wide enough to hold it");
    } else if (to->isPointer() && from->isInteger()) {
        // An integer becoming an address.
    } else if (to->unqualified() == from->unqualified()) {
        return v;  // reinterpret_cast<int>(n) of an int: legal, a no-op
    } else {
        src_.fail(pos, "'reinterpret_cast<" + to->describe() + ">' of a '" +
                       from->describe() + "' - reinterpret_cast reads one set "
                       "of bits as another kind of pointer or as an integer "
                       "wide enough to hold an address. A class and a floating "
                       "point value are not either, and converting between "
                       "numbers is 'static_cast'");
    }

    ExprPtr c(new Cast(to, std::move(v)));
    c->setType(to);
    return c;
}

// The type one keyword names, for [expr.type.conv], which takes exactly one
// simple-type-specifier: `unsigned(x)` is a cast and `unsigned long(x)` is
// ill-formed. Nothing is consumed; nullptr says the token is not one of these.
const Type *Parser::simpleTypeKeyword() const {
    static const struct { const char *word; Kind kind; } t[] = {
        { "void", Kind::Void },     { "bool", Kind::Bool },
        { "char", Kind::Char },     { "short", Kind::Short },
        { "int", Kind::Int },       { "long", Kind::Long },
        { "signed", Kind::Int },    { "unsigned", Kind::UInt },
        { "float", Kind::Float },   { "double", Kind::Double },
    };
    for (const auto &k : t)
        if (peek().is(k.word)) return types_.get(k.kind);
    if (peek().is("wchar_t")) return types_.get(Kind::WChar);
    if (peek().is("char16_t")) return types_.get(Kind::Char16);
    if (peek().is("char32_t")) return types_.get(Kind::Char32);
    return nullptr;
}

// `T{...}` for any T, the '{' still ahead - [expr.type.conv]/2, list-initialisation.
ExprPtr Parser::bracedValueInit(const Type *to, std::size_t pos) {
    return listTemporary(to, pos);
}

ExprPtr Parser::functionalCast(const Type *to, std::size_t pos) {
    // `T(x)` is a prvalue ([expr.type.conv]/2), whatever node the conversion hands back.
    ExprPtr r = [&]() -> ExprPtr {
        expect("(");
        auto zero = [&]() {
            // [expr.type.conv]/2: `void()` is a prvalue of void and initialises nothing -
            // `decltype(a.f(), void())` is how a trailing return type asks for an expression alone.
            if (to->isVoid()) {
                ExprPtr none(new Num(0LL));
                none->setType(types_.intType());
                ExprPtr c(new Cast(to, std::move(none)));
                c->setType(to);
                return c;
            }
            // The zero that convert can carry to any scalar: a null pointer
            // constant for a pointer, 0.0 for a floating type, 0 for the rest.
            ExprPtr z;
            if (to->isPointer()) {
                z.reset(new Num(0LL));
                z->setType(types_.get(Kind::NullPtr));
            } else if (to->isFloating()) {
                z.reset(new Num(0.0L));
                z->setType(types_.doubleType());
            } else {
                z.reset(new Num(0LL));
                z->setType(types_.intType());
            }
            return convert(std::move(z), types_.withoutConst(to), true);
        };
        if (consume(")")) return zero();
        // `T(std::forward<A>(a)...)` for a T that is no class: the expansion makes none or one operand.
        std::vector<ExprPtr> expanded;
        if (expandPackPattern(expanded)) {
            if (expanded.size() > 1)
                src_.fail(pos, "'" + to->describe() + "(a, b)' would need a constructor, and '" +
                               to->describe() + "' is not a class");
            expect(")");
            if (expanded.empty()) return zero();
            if (to->isVoid()) {
                ExprPtr c(new Cast(to, decay(std::move(expanded[0]))));
                c->setType(to);
                return c;
            }
            return convert(decay(std::move(expanded[0])), types_.withoutConst(to), true);
        }
        ExprPtr v = decay(assign());
        if (peek().is(","))
            src_.fail(peek().pos, "'" + to->describe() + "(a, b)' would need a "
                                  "constructor, and '" + to->describe() +
                                  "' is not a class");
        expect(")");
        if (to->isVoid()) {
            ExprPtr c(new Cast(to, std::move(v)));
            c->setType(to);
            return c;
        }
        refuseUnrelatedClassCast(*v, to, pos, "a functional cast");
        return convert(std::move(v), types_.withoutConst(to), true);
    }();
    r->setDeclCategory(Expr::Category::Prvalue);
    return r;
}

// **A function's address, from its key.**
const Parser::Signature *Parser::pickByTarget(const std::vector<std::size_t> &set, const Type *cls,
                                              const Type *target) const {
    const Type *t = target == nullptr ? nullptr : target->unqualified();
    if (t == nullptr) return nullptr;
    // A plain function and a static member want a pointer or reference to a function, `const &` to one
    // included; a non-static member wants a pointer to a member of `cls`.
    const Type *plain = t->isReference() ? t->referent()->unqualified() : t;
    plain = plain->isFunction() ? plain : plain->isFunctionPointer() ? plain->pointee() : nullptr;
    const Type *member = cls != nullptr && t->isMemberFunctionPointer() &&
                         t->enclosing() == cls->unqualified() ? t->pointee() : nullptr;
    const Signature *found = nullptr;
    for (std::size_t i : set) {
        const Signature &f = functions_[i];
        const Type *fn = !f.owner.empty() && !f.isStaticMember ? member : plain;
        if (fn == nullptr || types_.functionType(f.returns, f.params, f.variadic, f.refThis,
                                                 thisCvOf(f)) != fn) continue;
        if (found != nullptr) return nullptr;
        found = &f;
    }
    return found;
}

ExprPtr Parser::functionAsValue(const std::string &key, std::size_t pos) {
    const Signature *sig = nullptr;
    if (const std::vector<std::size_t> *set = overloadsOf(key)) {
        if (set->size() > 1) sig = pickByTarget(*set, nullptr, overloadTarget_);
        if (set->size() > 1 && sig == nullptr && overloadArgument_)
            return overloadPlaceholder(*set, key, nullptr, pos);
        if (set->size() > 1 && sig == nullptr)
            src_.fail(pos, "'" + key + "' names " +
                           std::to_string(set->size()) + " functions, and "
                           "which one this is cannot be told from the use "
                           "alone - choosing one by a type this place asks for is "
                           "not supported yet here; initialise a pointer of the "
                           "type wanted and use that");
    }
    if (sig == nullptr) sig = findFunction(key);
    if (sig == nullptr) return nullptr;
    return functionAddress(*sig, key);
}

ExprPtr Parser::functionAddress(const Signature &sig, const std::string &key) {
    if (sig.name == "main" && sig.cLinkage && sig.owner.empty()) {
        if (mainDefinedHere())
            src_.fail(peek().pos, "the address of 'main' cannot be taken - [basic.start.main]/3 forbids "
                                  "using it within the program");
        mainUses_.push_back(peek().pos);
    }
    Var *v = Var::global(key);
    v->setSymbol(sig.symbol);
    ExprPtr target(v);
    const Type *fn = types_.functionType(sig.returns, sig.params, sig.variadic);
    target->setType(fn);
    ExprPtr n(new Unary('&', std::move(target)));
    n->setType(types_.pointerTo(fn));
    return n;
}

// `&S::f` for a member function: the ABI's pair, built in a slot of this frame the way a class temporary is.
ExprPtr Parser::memberFunctionAddress(const Type *cls, const Signature &f, const std::string &key,
                                      std::size_t pos) {
    // [basic.def.odr]/2: taking its address uses it - a specialization's member is defined only where used.
    for (Signature &g : functions_)
        if (g.symbol == f.symbol) g.used = true;
    // [expr.unary.op]/3: a static member function's address is an ordinary pointer.
    if (f.isStaticMember) return functionAddress(f, key);
    // **A virtual one holds the slot, not the function.**
    // Itanium: 1 + the slot's byte offset, tested at the
    // call; Microsoft: the address of a vcall thunk.
    if (f.isVirtual) {
        int index = -1;
        const std::vector<VSlot> &slots = vtables_[cls->tag()];
        for (std::size_t i = 0; i < slots.size(); i++)
            if (overrides(slots[i], f.name, f.params, f.constThis, f.refThis, f.volatileThis))
                index = static_cast<int>(i);
        if (index < 0)
            src_.fail(pos, "'" + key + "' is virtual but has "
                           "no slot in '" + cls->describe() +
                           "'s own vtable - a function of a "
                           "base after the first is not "
                           "supported here yet");
        if (target_.microsoftNames())
            return boundMemberPointer(cls, f, pos, 0,
                                      synthesizeVcallThunk(cls, f, index, pos));
        return boundMemberPointer(cls, f, pos,
                                  1 + static_cast<long long>(index) * pointerBytes());
    }
    return boundMemberPointer(cls, f, pos);
}

bool Parser::atBareFunctionArgument(bool braced, bool shifted) const {
    std::size_t k = 0, open = 0;
    for (; peekAt(k).is("&") || peekAt(k).is("("); k++) open += peekAt(k).is("(") ? 1 : 0;
    if (peekAt(k).is("::")) k++;
    if (peekAt(k).kind != TokenKind::Ident) return false;
    while (peekAt(k + 1).is("::") && peekAt(k + 2).kind == TokenKind::Ident) k += 2;
    for (; open > 0; open--, k++)
        if (!peekAt(k + 1).is(")")) return false;
    const Token &after = peekAt(k + 1);
    if (shifted)
        return after.is("<<") || after.is(">>") || after.is(";") || after.is(")") || after.is(",") ||
               after.is("}") || after.is(":");
    return after.is(",") || after.is(braced ? "}" : ")");
}

// [over.over]/1: an overloaded name kept behind its placeholder is resolved by the type it initialises.
void Parser::resolveOverloadPlaceholder(ExprPtr &value, const Type *to, std::size_t pos) {
    const OverloadArg *oa = value != nullptr ? overloadArgOf(*value) : nullptr;
    if (oa == nullptr) return;
    ExprPtr picked = overloadArgumentFor(to, *oa);
    if (picked == nullptr)
        src_.fail(pos, "'" + oa->key + "' names " + std::to_string(oa->set.size()) + " functions, and "
                       "none of them is of the type '" + to->describe() + "' it initialises here");
    value = std::move(picked);
}

// [basic.lookup.unqual]/8: a member function named bare in a member is the class's before the namespace's.
ExprPtr Parser::memberFunctionByName(const std::string &name, std::size_t pos) {
    if (boundValueParams_.count(name) != 0) return nullptr;
    const Type *roots[2] = { currentClass_ != nullptr ? currentClass_
                                                      : classStack_.empty() ? nullptr
                                                                            : classStack_.back(),
                             lambdaScope() };
    for (const Type *root : roots)
        for (const Type *c = root; c != nullptr; c = c->base()) {
            const std::vector<std::size_t> *set =
                name == localOf(c->tag()) ? nullptr : overloadsOf(c->tag() + "::" + name);
            if (set == nullptr) continue;
            bool anyStatic = false;
            for (std::size_t i : *set) anyStatic = anyStatic || functions_[i].isStaticMember;
            if (!anyStatic)
                src_.fail(pos, "'" + name + "' names a non-static member function, which must be "
                               "called - write '&" + localOf(c->tag()) + "::" + name + "' for its "
                               "pointer to member");
            return functionAsValue(c->tag() + "::" + name, pos);
        }
    return nullptr;
}

// The placeholder is a `void` constant like a braced list's, told apart by its serial.
ExprPtr Parser::overloadPlaceholder(const std::vector<std::size_t> &set, const std::string &key,
                                    const Type *cls, std::size_t pos) {
    ExprPtr mark(new Num(++overloadSerial_));
    mark->setType(types_.get(Kind::Void));
    overloadArgs_.push_back(OverloadArg{ mark.get(), overloadSerial_, set, key, cls, pos });
    return mark;
}

const Parser::OverloadArg *Parser::overloadArgOf(const Expr &e) const {
    const Num *n = dynamic_cast<const Num *>(&e);
    if (n == nullptr || e.type() == nullptr || !e.type()->isVoid()) return nullptr;
    for (std::size_t i = 0; i < overloadArgs_.size(); i++)
        if (overloadArgs_[i].placeholder == &e && overloadArgs_[i].serial == n->value())
            return &overloadArgs_[i];
    return nullptr;
}

ExprPtr Parser::overloadArgumentFor(const Type *param, const OverloadArg &oa) {
    std::vector<std::size_t> set = oa.set;
    if (!oa.templates.empty()) {
        const OverloadArg with = oa;
        const std::vector<std::size_t> made = specializationsFor(with.templates, param, with.pos);
        set.insert(set.end(), made.begin(), made.end());
    }
    const Signature *f = pickByTarget(set, oa.cls, param);
    if (f != nullptr && f->fromTemplate) markUsed(f);
    if (f == nullptr) return nullptr;
    const OverloadArg mine = oa;
    for (std::size_t i = 0; i < overloadArgs_.size(); i++)
        if (overloadArgs_[i].placeholder == mine.placeholder) {
            overloadArgs_.erase(overloadArgs_.begin() + static_cast<long>(i));
            break;
        }
    return mine.cls != nullptr ? memberFunctionAddress(mine.cls, *f, mine.key, mine.pos)
                               : functionAddress(*f, mine.key);
}

void Parser::refuseOverloadArguments(const std::vector<ExprPtr> &args,
                                     const std::string &callee) const {
    for (std::size_t i = 0; i < args.size(); i++)
        if (const OverloadArg *oa = overloadArgOf(*args[i]))
            src_.fail(oa->pos, "'" + oa->key + "' names " + std::to_string(oa->set.size()) +
                               " functions, and choosing one by the parameter of '" + callee +
                               "' it is passed to is not supported yet on this road - initialise "
                               "a pointer of the type wanted and pass that");
}

// **The injected class name of a base**, which is how a derived class
// usually spells it: `Base::f(...)` for a `cc::Base`.
struct FindBase {
    static const Type *named(const Type *cls, const std::string &name) {
        if (cls == nullptr) return nullptr;
        const std::vector<Type::BaseSpec> &bs = cls->unqualified()->bases();
        for (std::size_t i = 0; i < bs.size(); i++) {
            if (bs[i].type->localName() == name ||
                bs[i].type->tag() == name) return bs[i].type;
            if (const Type *deeper = named(bs[i].type, name)) return deeper;
        }
        return nullptr;
    }
};

bool Parser::atUserDefinedLiteral() const {
    if (peek().kind == TokenKind::Num) return !peek().udSuffix.empty();
    for (std::size_t k = 0; peekAt(k).kind == TokenKind::Str; k++)
        if (!peekAt(k).udSuffix.empty()) return true;
    return false;
}

// A string literal's code units: bytes, UTF-16 units for `u`, code points for U and L.
static long long codeUnits(const std::string &text, char prefix, bool wide) {
    if (prefix != 'u' && prefix != 'U' && !wide) return static_cast<long long>(text.size());
    long long n = 0;
    for (std::size_t i = 0; i < text.size(); ) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        const int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        n += prefix == 'u' && extra == 3 ? 2 : 1;
        i += extra + 1;
    }
    return n;
}

// **The literal is rewritten as the call [lex.ext] says it is**, into tokens past the end and read
// by `primary` - so the call is looked up, resolved and folded as one written out would be. Which
// call a number makes is [lex.ext]/3-4's: the cooked form, else the raw one or the template.
ExprPtr Parser::userDefinedLiteral(Program *program) {
    const Token lit = peek();
    const std::size_t pos = lit.pos;
    std::string suffix = lit.udSuffix;
    Token value = lit;
    value.udSuffix.clear();
    at_++;
    // [lex.ext]/8: adjacent strings are one literal, the suffix any of them carries the whole's.
    if (lit.kind == TokenKind::Str) {
        while (peek().kind == TokenKind::Str) {
            if (!peek().udSuffix.empty() && !suffix.empty() && peek().udSuffix != suffix)
                src_.fail(peek().pos, "adjacent string literals with different ud-suffixes '" +
                                      suffix + "' and '" + peek().udSuffix + "' - [lex.ext]/8");
            if (!peek().udSuffix.empty()) suffix = peek().udSuffix;
            if (peek().prefix != 0 && value.prefix != 0 && peek().prefix != value.prefix)
                src_.fail(peek().pos, "adjacent string literals with different "
                                      "encoding prefixes - [lex.string]/13");
            if (peek().prefix != 0) value.prefix = peek().prefix;
            value.wide = value.wide || peek().wide;
            value.text += peek().text;
            at_++;
        }
    }
    const std::size_t resume = at_;
    const bool number = !lit.spelling.empty();
    const std::size_t suffixPos = number ? pos + lit.spelling.size() : pos;
    const char *what = lit.kind == TokenKind::Str ? "string literal"
                     : !number ? "character constant"
                     : lit.isFloat ? "floating constant" : "integer constant";
    // [usrlit.suffix]/1 reserves a suffix with no `_` first; clang calls one at a literal invalid.
    if (suffix[0] != '_')
        src_.fail(suffixPos, "invalid suffix '" + suffix + "' on " + what + " - a ud-suffix "
                             "that does not begin with '_' is reserved, [usrlit.suffix]");
    const std::string name = "operator\"\" " + suffix;
    const std::string shown = "operator\"\"" + suffix;
    const std::string key = qualifyForLookup(name, &Parser::hasFunctionNamed);
    const std::vector<std::size_t> *set = overloadsOf(key);

    std::vector<Token> call;
    auto put = [&](TokenKind kind, const std::string &text) {
        Token t;
        t.kind = kind;
        t.text = text;
        t.pos = pos;
        call.push_back(t);
    };
    auto byName = [&]() {
        put(TokenKind::Keyword, "operator");
        put(TokenKind::Str, std::string());
        put(TokenKind::Ident, suffix);
        put(TokenKind::Punct, "(");
    };
    if (number) {
        bool cooked = false, raw = false;
        if (set != nullptr)
            for (std::size_t k = 0; k < set->size(); k++) {
                const Signature &f = functions_[(*set)[k]];
                if (f.fromTemplate || f.params.size() != 1) continue;
                const Type *p = f.params[0]->unqualified();
                if (p->kind() == (lit.isFloat ? Kind::LongDouble : Kind::ULongLong)) cooked = true;
                if (p->isPointer() && p->pointee()->unqualified()->kind() == Kind::Char) raw = true;
            }
        const bool templ = isTemplateName(name);
        if (cooked) {
            // [lex.ext]/3-4: `n ULL` or `f L`, the literal's own value.
            if (lit.tooLarge)
                src_.fail(pos, "integer literal is too large to be represented in any integer "
                               "type - [lex.ext]/3 hands '" + shown + "' an unsigned long long");
            value.suffixU = value.suffixLL = !lit.isFloat;
            value.suffixL = true;
            value.suffixF = false;
            byName();
            call.push_back(value);
        } else if (raw && templ) {
            src_.fail(pos, "'" + shown + "' is both a raw literal operator and a literal "
                           "operator template, and [lex.ext]/3 asks for one of them");
        } else if (raw) {
            byName();
            Token str;
            str.kind = TokenKind::Str;
            str.text = lit.spelling;
            str.pos = pos;
            call.push_back(str);
        } else if (templ) {
            // `operator"" _t<'1', '2'>()`, its characters the literal's as written.
            put(TokenKind::Ident, name);
            put(TokenKind::Punct, "<");
            for (std::size_t k = 0; k < lit.spelling.size(); k++) {
                if (k > 0) put(TokenKind::Punct, ",");
                Token c;
                c.kind = TokenKind::Num;
                c.value = static_cast<signed char>(lit.spelling[k]);
                c.isChar = true;
                c.pos = pos;
                call.push_back(c);
            }
            put(TokenKind::Punct, ">");
            put(TokenKind::Punct, "(");
        } else {
            src_.fail(suffixPos, "no matching literal operator for call to '" + shown +
                                 "' with argument of type '" +
                                 (lit.isFloat ? "long double" : "unsigned long long") +
                                 "' or 'const char *', and no matching literal operator "
                                 "template - [lex.ext]/3");
        }
    } else {
        if (set == nullptr)
            src_.fail(pos, "no matching literal operator for call to '" + shown + "' with this " +
                           what + " - [lex.ext]/" + (lit.kind == TokenKind::Str ? "5" : "6"));
        byName();
        call.push_back(value);
        // [lex.ext]/5: a string's length in code units, its '\0' not counted, as a size_t.
        if (lit.kind == TokenKind::Str) {
            put(TokenKind::Punct, ",");
            Token len;
            len.kind = TokenKind::Num;
            len.value = codeUnits(value.text, value.prefix, value.wide);
            const Kind size = target_.sizeType();
            len.suffixU = true;
            len.suffixL = size == Kind::ULong || size == Kind::ULongLong;
            len.suffixLL = size == Kind::ULongLong;
            len.pos = pos;
            call.push_back(len);
        }
    }
    put(TokenKind::Punct, ")");
    put(TokenKind::End, std::string());

    const std::size_t start = tokens_.size();
    tokens_.insert(tokens_.end(), call.begin(), call.end());
    at_ = start;
    ExprPtr made = primary(program);
    at_ = resume;
    return made;
}

// [class.mfct.non-static]/3: in a member function of the class or a derived
// one, `PB::v` is `(*this).PB::v` (also `Base<T>::v` in a class template).
// Null where there is no `this` of a class the member belongs to.
ExprPtr Parser::qualifiedMemberThroughThis(const Type *cls, const Member &m,
                                            const std::string &member, std::size_t pos) {
    const Local *self = findLocal("this");
    const Type *mine = self != nullptr && self->type->isPointer() ? self->type->pointee() : nullptr;
    if (mine == nullptr || m.inVirtualBase != nullptr || m.ambiguous) return nullptr;
    if (mine->unqualified() != cls && accessibleBaseOffset(mine, cls) < 0) return nullptr;
    ExprPtr me(Var::local("this", self->offset));
    me->setType(self->type);
    ExprPtr obj(new Unary('*', std::move(me)));
    obj->setType(mine);
    if (mine->unqualified() != cls) obj = baseSubobject(std::move(obj), cls);
    checkAccessible(obj->type(), m, pos);
    const Type *objType = obj->type();
    ExprPtr acc(new MemberAccess(std::move(obj), member, m.offset, m.width, m.bitOffset));
    acc->setType(memberTypeIn(objType, m));
    return useReference(std::move(acc));
}

ExprPtr Parser::primary(Program *program) {
    // **`typename X<T>::type()` makes a temporary** - [temp.res]/3 says the name is a type,
    // and the body replays with T bound, so the hint is dropped here as in a declaration.
    if (peek().is("typename")) {
        const std::size_t pos = peek().pos;
        at_++;
        if (!(peek().kind == TokenKind::Ident || peek().is("::")))
            src_.fail(pos, "'typename' introduces a qualified type name, and this is not one");
    }
    // [expr.prim.general]/8: a decltype-specifier as the class of a nested-name-specifier - `decltype(c)::den`.
    if (peek().is("decltype")) {
        std::size_t k = 1;
        for (int depth = 0; peekAt(k).kind != TokenKind::End; k++) {
            if (peekAt(k).is("(")) depth++;
            else if (peekAt(k).is(")") && --depth == 0) break;
        }
        if (peekAt(k + 1).is("::") && peekAt(k + 2).kind == TokenKind::Ident) {
            const std::size_t pos = peek().pos;
            const Type *cls = realizeMemberClass(decltypeSpecifier()->unqualified(), true);
            if (!cls->isStructOrUnion())
                src_.fail(pos, "'" + cls->describe() + "' is not a class, so nothing can be named in it with '::'");
            at_++;
            return templateIdMember(cls, pos);
        }
    }
    if (peek().is("typeid")) {
        std::size_t pos = peek().pos;
        at_++;
        return typeidExpression(pos);
    }
    if (peek().kind == TokenKind::Ident && peekAt(1).is("(") && isTypeTraitName(peek().text))
        return typeTrait();
    if (peek().kind == TokenKind::Ident && peek().text.compare(0, 6, "$trait") == 0) {
        const std::size_t k = static_cast<std::size_t>(std::stoul(peek().text.substr(6)));
        at_++;
        return traitOperand(traitOperands_.at(k));
    }
    if (peek().is("static_cast")) {
        std::size_t pos = peek().pos;
        at_++;
        return staticCast(pos);
    }

    if (peek().is("const_cast")) {
        std::size_t pos = peek().pos;
        at_++;
        return constCast(pos);
    }

    if (peek().is("reinterpret_cast")) {
        std::size_t pos = peek().pos;
        at_++;
        return reinterpretCast(pos);
    }

    // **`dynamic_cast` asks what an object really is**, which the type_info
    // beside its vtable answers - see emitClassTypeInfo.
    if (peek().is("dynamic_cast")) {
        const std::size_t pos = peek().pos;
        at_++;
        return dynamicCast(pos);
    }

    // **`nullptr` is a zero that knows it is not an integer.** At the machine it is
    // a pointer-sized 0 and the backends hear nothing about it; what the type buys
    // is `f(int)` losing to `f(char *)`, and `int n = nullptr;` being refused.
    if (peek().is("nullptr")) {
        at_++;
        ExprPtr n(new Num(static_cast<long long>(0)));
        n->setType(types_.get(Kind::NullPtr));
        return n;
    }

    if (peek().is("true") || peek().is("false")) {
        bool value = peek().is("true");
        at_++;
        ExprPtr n(new Num(static_cast<long long>(value ? 1 : 0)));
        n->setType(types_.get(Kind::Bool));
        return n;
    }

    // `this` is the hidden first parameter, read back. It is a pointer and not
    // a reference, which is C++'s own choice and the reason `->` is written so
    // often inside a member function.
    if (peek().is("this")) {
        std::size_t pos = peek().pos;
        at_++;
        // **Inside a lambda that captured it, `this` is the enclosing
        // object's** - [expr.prim.lambda]. The closure's own `this` is not
        // what the reader wrote and is of no use to them.
        if (ExprPtr held = capturedThisPointer()) return held;
        // [expr.prim.general]/3: `this` may be written in a member function's declarator after its
        // cv-qualifiers - a trailing return type, an exception-specification - where nothing evaluates it:
        // `auto g() -> decltype(this->v);` in the class body (1.80).
        if (unevaluated_ > 0 && !classStack_.empty() && !classStack_.back()->isComplete() && !inStaticMember_) {
            ExprPtr v(Var::local("this", thisOffset_));
            v->setType(types_.pointerTo(classStack_.back()->unqualified()));
            return v;
        }
        if (currentClass_ == nullptr)
            src_.fail(pos, "'this' is only inside a member function, and this "
                           "is not one");
        // [class.static]/1: a static member function has no `this`.
        if (inStaticMember_)
            src_.fail(pos, "'this' is not available in a static member "
                           "function - it is not called on an object, so there "
                           "is none to point at");
        const Local *slot = findLocal("this");
        // (a `this` left standing by the last function defined is not this class's - see the bare call below)
        if (slot != nullptr && (slot->type == nullptr || !slot->type->isPointer() ||
                                slot->type->pointee()->unqualified() != currentClass_->unqualified()))
            slot = nullptr;
        ExprPtr v(Var::local("this", slot != nullptr ? slot->offset : thisOffset_));
        v->setType(slot != nullptr ? slot->type
                                   : types_.pointerTo(currentClass_));
        return v;
    }

    // **`operator+(a, b)` written out.**
    if (peek().is("[")) return lambdaExpression();

    if (peek().is("operator")) {
        std::size_t opos = peek().pos;
        const std::string name = operatorName();
        if (!consume("("))
            src_.fail(peek().pos, "'" + name + "' is a function here, and a "
                                  "call needs its arguments");
        std::vector<ExprPtr> args;
        parseArguments(args);
        const Signature &sig = resolveOverloadBraced(name, args, opos);
        applyDefaults(sig, args, opos);
        return completeCall(name, sig.symbol, nullptr, sig.returns, sig.params,
                            sig.variadic, opos, std::move(args));
    }

    if (peek().kind == TokenKind::Keyword) {
        if (const char *pending = notYetSupported(peek().text))
            src_.fail(peek().pos, std::string("'") + pending +
                                  "' is not supported yet");
        if (const char *here = implementedElsewhere(peek().text))
            src_.fail(peek().pos, std::string("'") + here + "' is implemented, "
                                  "but it does not begin an expression");
    }

    // A template named in an expression.
    if (peek().kind == TokenKind::Ident && isTemplateName(peek().text)) {
        // **Class scope before namespace scope** - [basic.lookup.unqual]/8,
        // the rule R1 fixed for a name and this is the same rule for a *call*.
        // A data member hides one too - `vector[0]` in a class with a member `vector` - unless a `<` follows.
        const bool memberFirst =
            currentClass_ != nullptr && !blockUsingFunctions_.count(peek().text) &&
            ((peekAt(1).is("(") &&
              (findMemberOwner(currentClass_, peek().text) != nullptr ||
               memberTemplateOwner(currentClass_, peek().text) != nullptr)) ||
             // `get<T>(key)` names the class's member template before std::get (1.82: the namespace's
             // template was called, and the class's second `get` was never instantiated).
             (peekAt(1).is("<") && memberTemplateOwner(currentClass_, peek().text) != nullptr) ||
             (!peekAt(1).is("<") && (currentClass_->findMember(peek().text) != nullptr ||
                                     currentClass_->findStaticMember(peek().text) != nullptr)));
        if (!memberFirst) return templateCall(program);
    }

    // HUGE_VAL, which a literal could only spell by being out of range.
    if (peek().is("__builtin_huge_val") && peekAt(1).is("(")) {
        at_ += 2;
        expect(")");
        ExprPtr n(new Num(std::numeric_limits<long double>::infinity()));
        n->setType(types_.get(Kind::Double));
        return n;
    }
    if (peek().is("__builtin_va_start")) {
        std::size_t pos = peek().pos;
        at_++;

        if (!variadicBody_)
            src_.fail(pos, "va_start is only allowed in a function declared "
                           "with '...'");
        expect("(");

        ExprPtr list = decay(assign());
        expect(")");
        if (!list->type()->isPointer())
            src_.fail(pos, "va_start needs a va_list");
        ExprPtr n(new VaStart(std::move(list)));
        n->setType(types_.voidType());
        return n;
    }

    if (peek().is("__builtin_va_arg")) {
        std::size_t pos = peek().pos;
        at_++;
        // Any function may read a va_list it was handed - vprintf is that - and only va_start
        // needs the variadic frame (C 7.15.1): every code generator's va_arg reads the list alone.
        expect("(");
        ExprPtr list = decay(assign());
        if (!list->type()->isPointer())
            src_.fail(pos, "va_arg needs a va_list");
        expect(",");
        StorageClass sc;
        const Type *want = specifiers(&sc);
        want = declarator(want, true).type;
        expect(")");

        if (!want->isComplete())
            src_.fail(pos, "va_arg needs a complete type");

        const char *promotes = nullptr;
        switch (want->kind()) {
        case Kind::Char: case Kind::SChar: case Kind::UChar:
        case Kind::Short: case Kind::UShort: case Kind::WChar: promotes = "int"; break;
        case Kind::Char16:                   promotes = "int"; break;
        case Kind::Char32:                   promotes = "unsigned int"; break;
        case Kind::Float:                    promotes = "double"; break;
        default: break;
        }
        if (promotes != nullptr)
            src_.fail(pos, "'" + want->describe() + "' is promoted before it "
                           "reaches a variadic function, so va_arg cannot ask "
                           "for it - ask for '" + promotes + "'");

        if (want->isArray())
            src_.fail(pos, "an array decays on its way to a variadic function, so va_arg cannot ask for one");
        // [expr.call]/7: one with a non-trivial copy or destructor is conditionally-supported, and is not here.
        if (want->isStructOrUnion() && (want->nonTrivialCopy() || want->hasDestructor()))
            src_.fail(pos, "'" + want->describe() + "' has a non-trivial copy or destructor, which going through "
                           "'...' is conditionally-supported by [expr.call]/7, and va_arg does not support it");

        VaArg *read = new VaArg(std::move(list));
        if (want->isStructOrUnion()) {
            const long long words = (want->size(target_) + 7) / 8;
            read->setSlot(allocateFrameSlot(types_.arrayOf(types_.get(Kind::LongLong), words > 0 ? words : 1)));
        }
        ExprPtr n(read);
        n->setType(want);
        return n;
    }

    if (peek().is("(")) {
        const Nesting deep(this, peek().pos, "parenthesised expression");
        at_++;
        // Inside parentheses a `>` is an operator again, which is exactly why
        // C++ makes a comparison in a template argument need them.
        const bool wasInArgs = inTemplateArgs_;
        inTemplateArgs_ = false;
        ExprPtr e = expr();
        inTemplateArgs_ = wasInArgs;
        expect(")");
        return e;
    }

    if (atUserDefinedLiteral()) return userDefinedLiteral(program);

    if (peek().kind == TokenKind::Str) {
        std::string label = ".L.str." + std::to_string(strings_++);
        std::string text = peek().text;
        at_++;

        bool wide = tokens_[at_ - 1].wide;
        char prefix = tokens_[at_ - 1].prefix;
        while (peek().kind == TokenKind::Str) {
            text += peek().text;

            wide = wide || peek().wide;
            if (peek().prefix != 0 && prefix != 0 && peek().prefix != prefix)
                src_.fail(peek().pos, "adjacent string literals with different "
                                      "encoding prefixes - [lex.string]/13");
            if (peek().prefix != 0) prefix = peek().prefix;
            at_++;
        }

        const Type *elem = prefix == 'u' ? types_.get(Kind::Char16)
                         : prefix == 'U' ? types_.get(Kind::Char32)
                         : wide          ? types_.get(Kind::WChar)
                                         : types_.charType();
        int width = elem->size(target_);

        // **A u, U or L literal is UTF-8 in the source and code units in the object**
        // - [lex.string]/8 and /9; a surrogate pair in u for a point past the BMP.
        std::vector<unsigned long long> units;
        if (prefix == 'u' || prefix == 'U' || wide) {
            for (std::size_t i = 0; i < text.size(); ) {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
                unsigned long long cp = extra == 0 ? c : c & (0x3F >> extra);
                i++;
                for (int k = 0; k < extra && i < text.size(); k++, i++)
                    cp = (cp << 6) | (static_cast<unsigned char>(text[i]) & 0x3F);
                if (prefix == 'u' && cp >= 0x10000) {
                    units.push_back(0xD800 + ((cp - 0x10000) >> 10));
                    units.push_back(0xDC00 + ((cp - 0x10000) & 0x3FF));
                } else {
                    units.push_back(cp);
                }
            }
        } else {
            for (unsigned char ch : text) units.push_back(ch);
        }
        std::string bytes;
        for (unsigned long long u : units)
            for (int k = 0; k < width; k++) bytes.push_back(static_cast<char>((u >> (8 * k)) & 0xFF));
        for (int k = 0; k < width; k++) bytes.push_back('\0');

        program->strings.push_back(StringLit{ label, bytes, width });
        ExprPtr n(new StrLit(label, text));
        n->setType(types_.arrayOf(types_.withConst(elem),
                                  static_cast<long long>(units.size()) + 1));
        return n;
    }

    if (peek().kind == TokenKind::Num && peek().isFloat) {
        const Token &t = peek();
        // **A `long double` this compiler cannot spell the same way on every machine
        // is refused.** A constant is carried as a double, and the digits answer
        // whether it is exactly one; approximating per build host is not on offer.
        if (t.suffixL && !t.exactInDouble &&
            target_.sizeOf(Kind::LongDouble) > target_.sizeOf(Kind::Double))
            src_.fail(t.pos, "this 'long double' literal needs more precision "
                             "than a double holds, and a double is what this "
                             "compiler carries a floating constant in, so "
                             "that every build machine reads the same "
                             "number. It is refused rather than "
                             "approximated: write it as a 'double', or as a "
                             "hexadecimal float once those arrive");
        // [lex.fcon]/1: past its type's range a literal is ill-formed; this is that diagnostic.
        if (t.dvalue > (t.suffixF ? static_cast<long double>(std::numeric_limits<float>::max())
                                  : static_cast<long double>(std::numeric_limits<double>::max())))
            src_.warn(t.pos, "this floating constant is outside the range of its type, and is taken as infinity");
        ExprPtr n(new Num(t.dvalue));
        n->setType(types_.get(t.suffixF ? Kind::Float
                            : t.suffixL ? Kind::LongDouble : Kind::Double));
        at_++;
        return n;
    }

    if (peek().kind == TokenKind::Num) {
        const Token &t = peek();
        const Type *ty;
        unsigned long long u = static_cast<unsigned long long>(t.value);

        auto fits = [&](Kind k) {
            const Type *c = types_.get(k);
            int bits = c->size(target_) * 8;
            unsigned long long limit =
                c->isSigned(target_) ? (1ULL << (bits - 1)) - 1
                                     : (bits >= 64 ? ~0ULL : (1ULL << bits) - 1);
            return u <= limit;
        };

        if (t.suffixU && t.suffixLL)     ty = types_.get(Kind::ULongLong);
        else if (t.suffixLL)             ty = fits(Kind::LongLong)
                                            ? types_.get(Kind::LongLong)
                                            : types_.get(Kind::ULongLong);
        else if (t.suffixU && t.suffixL) ty = fits(Kind::ULong)
                                            ? types_.get(Kind::ULong)
                                            : types_.get(Kind::ULongLong);
        else if (t.suffixU)              ty = fits(Kind::UInt)  ? types_.get(Kind::UInt)
                                            : fits(Kind::ULong) ? types_.get(Kind::ULong)
                                            : types_.get(Kind::ULongLong);
        else if (t.suffixL)              ty = fits(Kind::Long)  ? types_.get(Kind::Long)
                                            : (!t.decimal && fits(Kind::ULong))
                                                               ? types_.get(Kind::ULong)
                                            : fits(Kind::LongLong)
                                                               ? types_.get(Kind::LongLong)
                                            : types_.get(Kind::ULongLong);

        else if (t.prefix == 'u')        ty = types_.get(Kind::Char16);
        else if (t.prefix == 'U')        ty = types_.get(Kind::Char32);
        else if (t.wide)                 ty = types_.get(Kind::WChar);
        // [lex.ccon]/2: an ordinary character literal has type char, where C gives it int.
        else if (t.isChar)               ty = types_.get(Kind::Char);
        // **[lex.icon] table 6, and the two ladders it holds.** A decimal literal
        // climbs int, long, long long and never reaches unsigned; a hex or octal
        // one has an unsigned rung above each. One ladder served both, wrongly.
        else if (fits(Kind::Int))        ty = types_.intType();
        else if (!t.decimal && fits(Kind::UInt))
                                         ty = types_.get(Kind::UInt);
        else if (fits(Kind::Long))       ty = types_.get(Kind::Long);
        else if (!t.decimal && fits(Kind::ULong))
                                         ty = types_.get(Kind::ULong);
        else if (fits(Kind::LongLong))   ty = types_.get(Kind::LongLong);
        else                             ty = types_.get(Kind::ULongLong);
        ExprPtr n(new Num(t.value));
        n->setType(ty);
        at_++;
        return n;
    }

    // The qualified shapes an expression can open with, each told from the next by
    // what the chain reaches: `Counter::total` a static member, `N::f` a namespace,
    // `N::S(4)` a temporary, and `int()` or `int(x)` a fundamental type as one.
    if (const Type *to = simpleTypeKeyword()) {
        const std::size_t tpos = peek().pos;
        const std::string word = peek().text;
        at_++;
        if (simpleTypeKeyword() != nullptr)
            src_.fail(tpos, "'" + word + " " + peek().text + "(...)' is not a "
                            "functional cast - [expr.type.conv] takes one "
                            "type name, so write '(" + word + " " +
                            peek().text + ")x', or a typedef of the type");
        if (peek().is("{")) return bracedValueInit(to, tpos);
        if (!peek().is("("))
            src_.fail(tpos, "'" + word + "' is a type, and in an expression a "
                            "type needs '(' or '{' after it - '" + word + "(x)' "
                            "converts x, '" + word + "()' and '" + word + "{}' "
                            "are its zero");
        return functionalCast(to, tpos);
    }

    // unqualified `S(4)` is caught further down, where the name is already in
    // hand; a qualified one has to be recognised before either '::' branch
    // takes it, since to them it is a name followed by a call.
    if (const std::size_t typeEnd = qualifiedTypeEnd()) {
        if (peekAt(typeEnd).is("<")) {
            std::string q = peek().text;
            for (std::size_t k = 1; k + 1 <= typeEnd - 1; k += 2)
                q += "::" + peekAt(k + 1).text;
            std::map<std::string, TemplateDecl>::const_iterator td =
                findTemplate(q);
            const std::size_t after = qualifiedTypeEndPastArgs();
            if (td != templates_.end() && td->second.isClass && after != 0 &&
                peekAt(after).is("::")) {
                const std::size_t qpos = peek().pos;
                at_ += typeEnd;                      // to the `<`
                const Type *cls = instantiateClass(td->second, qpos);
                if (cls != nullptr && cls->isStructOrUnion() &&
                    peek().is("::")) {
                    at_++;
                    return templateIdMember(cls, qpos);
                }
            }
        }
        // `N::S{1, 2}` - the same temporary, list-initialised.
        if (peekAt(typeEnd).is("{")) {
            const Type *named = qualifiedTypeAt(typeEnd);
            if (named != nullptr) {
                const std::size_t qpos = peek().pos;
                at_ += typeEnd;
                return listTemporary(named, qpos);
            }
        }
        if (peekAt(typeEnd).is("(")) {
            const Type *named = qualifiedTypeAt(typeEnd);
            const std::size_t qpos = peek().pos;
            if (named != nullptr && named->isStructOrUnion()) {
                at_ += typeEnd + 1;                 // the name and the '('
                return classTemporary(named, qpos);
            }
            // **`S::T(3)` - [expr.type.conv] through a member typedef**, as `T(3)` is unqualified.
            if (named != nullptr) {
                at_ += typeEnd;
                return functionalCast(named, qpos);
            }
        }
    }

    // **`::f()` - a name asked for at global scope explicitly.** Refused by
    // name rather than left to "expected an expression", which points at a
    // '::' and says nothing.
    if (ExprPtr e = enumeratorThroughEnum()) return e;
    // **`::g`, `::f()` - [basic.lookup.qual]/4: the name looked up in the global namespace alone**, so a
    // local, a member or an enclosing namespace's name of the same spelling is passed over; `::N::x` is
    // N's x. The arguments of a call are read first, in the scope the call is written in.
    if (peek().is("::") && peekAt(1).kind == TokenKind::Ident) {
        if (peekAt(2).is("::")) { at_++; return primary(program); }
        const std::size_t qpos = peek().pos;
        const std::string name = peekAt(1).text;
        // Only a template the global namespace declares counts: `::sqrt(x)` inside std, which has a
        // sqrt template of its own, is C's function.
        bool globalTemplate = isTemplateName(name, true);
        const std::map<std::string, std::vector<TemplateDecl> >::const_iterator ft = fnTemplates_.find(name);
        if (globalTemplate && ft != fnTemplates_.end()) {
            globalTemplate = false;
            for (const TemplateDecl &d : ft->second) globalTemplate = globalTemplate || d.ns.empty();
        }
        // `::g<int>(1)`: the template-id is read from the global namespace, as the plain call below is.
        if (globalTemplate && !peekAt(2).is("(")) {
            struct Back {
                std::vector<std::string> &stack; std::vector<std::string> was;
                ~Back() { stack = was; }
            } back{ namespaceStack_, namespaceStack_ };
            at_++;
            namespaceStack_.clear();
            return templateCall(program, std::string());
        }
        at_ += 2;
        struct Outer {
            std::vector<std::string> &stack; std::vector<std::string> was;
            ~Outer() { stack = was; }
        } outer{ namespaceStack_, namespaceStack_ };
        if (consume("(")) {
            std::vector<ExprPtr> args;
            parseArguments(args);
            namespaceStack_.clear();
            // The global namespace's templates alone, not the specializations other namespaces' left under the name.
            std::string key = name;
            if (globalTemplate) {
                std::vector<const Type *> argTypes;
                for (const ExprPtr &a : args) argTypes.push_back(deductionArgType(*a));
                std::string reachedIn;
                std::set<std::string> deduced;
                instantiateViableTemplates(templatesNamed(name, std::string(), &reachedIn), argTypes, qpos, &deduced);
                std::vector<std::size_t> mine;
                if (const std::vector<std::size_t> *all = overloadsOf(name))
                    for (std::size_t i : *all) if (!functions_[i].fromTemplate) mine.push_back(i);
                if (const std::vector<std::size_t> *own = overloadsOf("::" + name))
                    mine.insert(mine.end(), own->begin(), own->end());
                key = "$global:" + name;
                functionIndex_[key] = mine;
            }
            const Signature sig = resolveOverloadBraced(key, args, qpos);
            applyDefaults(sig, args, qpos);
            return completeCall(name, sig.symbol, nullptr, sig.returns, sig.params, sig.variadic, qpos,
                                std::move(args));
        }
        namespaceStack_.clear();
        if (ExprPtr v = globalRef(name)) return v;
        if (const EnumConst *e = findEnum(name)) {
            ExprPtr n(new Num(e->value));
            n->setType(e->type != nullptr ? e->type : types_.intType());
            return n;
        }
        if (ExprPtr fn = functionAsValue(name, qpos)) return fn;
        src_.fail(qpos, "'::" + name + "' was not declared in the global namespace");
    }
    // **`::operator new(n)`, `::operator delete(p)`** - [expr.call] of the global allocation and deallocation
    // functions by name, [basic.stc.dynamic]/2 declaring them in every unit (item 32).
    if (peek().is("::") && peekAt(1).is("operator") && (peekAt(2).is("new") || peekAt(2).is("delete"))) {
        const std::size_t qpos = peek().pos;
        std::string name = "operator" + peekAt(2).text;
        at_ += 3;
        if (peek().is("[") && peekAt(1).is("]")) { at_ += 2; name += "[]"; }
        expect("(");
        std::vector<ExprPtr> args;
        parseArguments(args);
        struct Outer {
            std::vector<std::string> &stack; std::vector<std::string> was;
            ~Outer() { stack = was; }
        } outer{ namespaceStack_, namespaceStack_ };
        namespaceStack_.clear();
        if (overloadsOf(name) == nullptr)
            src_.fail(qpos, "'::" + name + "' is not declared - include <new>");
        const Signature sig = resolveOverload(name, args, qpos);
        applyDefaults(sig, args, qpos);
        return completeCall(name, sig.symbol, nullptr, sig.returns, sig.params, sig.variadic, qpos,
                            std::move(args));
    }
    if (peek().is("::"))
        src_.fail(peek().pos, "'::' here is followed by no name this compiler can look up in the "
                              "global namespace - a destructor is not supported yet");

    // ...and `N::S::n` is not this: the chain reaches a *class*, and what follows a
    // class is a member, which the branch below already knows how to find. Asked
    // before the namespace is eaten, since eating it loses the start of the name.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("::") &&
        !namespaceNamed(peek().text).empty() &&
        // **...unless what follows the type is a `(`**, which makes it a
        // temporary rather than the qualifier of a member: `std::vector<T>()`
        // belongs to this branch, `std::vector<T>::size_type` does not.
        (qualifiedTypeEnd() == 0 ||
         peekAt(qualifiedTypeEndPastArgs()).is("(") ||
         peekAt(qualifiedTypeEndPastArgs()).is("{"))) {
        const std::size_t qpos = peek().pos;
        std::string scope = namespaceNamed(peek().text);
        at_ += 2;
        while (peek().kind == TokenKind::Ident && peekAt(1).is("::") &&
               namespaces_.find(scope + "::" + peek().text) != namespaces_.end()) {
            scope += "::" + peek().text;
            at_ += 2;
        }
        // **`std::copy(a, b, c)` - a function template named qualified**, one the namespace itself declares.
        // [namespace.qual]: `k::collate` is k's - a class template of that name in an enclosing namespace is not.
        if (peek().kind == TokenKind::Ident && isTemplateName(peek().text, true)) {
            const auto t = findTemplate(scope + "::" + peek().text);
            std::string reachedIn;
            if ((t != templates_.end() && t->second.isClass && t->second.ns == scope + "::") ||
                !templatesNamed(peek().text, scope, &reachedIn).empty())
                return templateCall(program, scope);
        }

        std::string full = scope + "::" + expectIdent("a name");
        // **A member of N's unnamed namespace is reached as `N::x`** -
        // [namespace.unnamed]/1 puts a using-directive for it in N, which a
        // qualified lookup honours: what N itself lacks is asked of it.
        {
            const std::string inner = scope + "::_GLOBAL__N_1::" + full.substr(scope.size() + 2);
            if (overloadsOf(full) == nullptr && findGlobalToUpdate(full) == nullptr &&
                findEnum(full) == nullptr && namespaces_.count(scope + "::_GLOBAL__N_1") &&
                (overloadsOf(inner) != nullptr || findGlobalToUpdate(inner) != nullptr ||
                 findEnum(inner) != nullptr))
                full = inner;
        }
        if (consume("(")) {
            std::vector<ExprPtr> args;
            parseArguments(args);
            const Signature &sig = resolveOverloadBraced(full, args, qpos);
            applyDefaults(sig, args, qpos);
            return completeCall(full, sig.symbol, nullptr, sig.returns,
                                sig.params, sig.variadic, qpos, std::move(args),
                                !sig.owner.empty());
        }
        if (ExprPtr v = objectRef(full)) return v;
        if (const EnumConst *e = findEnum(full)) {
            ExprPtr n(new Num(e->value));
            n->setType(e->type != nullptr ? e->type : types_.intType());
            return n;
        }
        // `take(N::nl)` - the function itself, written qualified and not called.
        if (ExprPtr f = functionAsValue(full, qpos)) return f;
        src_.fail(qpos, "'" + full + "' was not declared in '" + scope + "'");
    }

    if (peek().kind == TokenKind::Ident && peekAt(1).is("::") &&
        peekAt(2).kind == TokenKind::Ident) {
        // The longest prefix naming a class with such a member wins
        // (`Outer::Inner::shared` finds Inner's); each prefix is the class it
        // names, so `WI::flag::value` reaches flag as a member type of WI's.
        std::string q = peek().text;
        const Type *cls = findTypedef(q);
        const Type *owner = nullptr;
        std::string member;
        std::size_t consumed = 0;
        for (std::size_t k = 1; peekAt(k).is("::") &&
                                peekAt(k + 1).kind == TokenKind::Ident; k += 2) {
            const std::string component = peekAt(k + 1).text;
            if (cls != nullptr && cls->isStructOrUnion() &&
                cls->findStaticMember(component) != nullptr) {
                owner = cls;
                member = component;
                consumed = k + 2;
            }
            q += "::" + component;
            cls = memberClassAlong(q, cls, component);
        }
        if (owner != nullptr) {
            const std::size_t qpos = peek().pos;
            at_ += consumed;
            return staticMemberRef(owner, *owner->findStaticMember(member),
                                   owner->tag(), qpos);
        }

        // **`C::StepBase` - an enumerator named through its class**, which is
        // the same walk one step over: the longest prefix that names a class
        // and has an enumerator of that name.
        std::string e = peek().text;
        const Type *in = findTypedef(e);
        const EnumConst *found = nullptr;
        const Type *foundIn = nullptr;
        std::size_t took = 0;
        for (std::size_t k = 1; peekAt(k).is("::") &&
                                peekAt(k + 1).kind == TokenKind::Ident; k += 2) {
            const std::string component = peekAt(k + 1).text;
            if (in != nullptr && in->isStructOrUnion())
                if (const EnumConst *c = enumInClass(in, component)) {
                    found = c;
                    foundIn = in;
                    took = k + 2;
                }
            e += "::" + component;
            in = memberClassAlong(e, in, component);
        }
        if (found != nullptr) {
            if (!currentFunction_.empty()) checkMemberEnumAccess(foundIn, peekAt(took - 1).text, peekAt(took - 1).pos);
            at_ += took;
            ExprPtr n(new Num(found->value));
            n->setType(found->type != nullptr ? found->type : types_.intType());
            return n;
        }
        if (unevaluated_ > 0)
            if (ExprPtr m = unevaluatedMember()) return m;
    }

    // **`S::f(...)` or `S::f<T>(...)` where f is a member function template**: static, or reached
    // through `*this` from a member of S or of a class derived from it.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("::")) {
        std::string q = peek().text;
        for (std::size_t k = 1; peekAt(k).is("::") && peekAt(k + 1).kind == TokenKind::Ident; k += 2) {
            const std::string component = peekAt(k + 1).text;
            const Type *cls = findTypedef(q);
            if (cls != nullptr && cls->isStructOrUnion() &&
                (peekAt(k + 2).is("(") || peekAt(k + 2).is("<")) &&
                memberTemplateOwner(cls, component) != nullptr) {
                const std::size_t qpos = peek().pos;
                at_ += k + 2;                  // the name; memberTemplateCall reads from the '<' or '('
                const Local *self = findLocal("this");
                const bool related = self != nullptr && !inStaticMember_ && currentClass_ != nullptr &&
                                     (currentClass_->unqualified() == cls->unqualified() ||
                                      accessibleBaseOffset(currentClass_, cls->unqualified()) >= 0);
                if (!related) return memberTemplateCall(nullptr, cls, component, qpos);
                ExprPtr me(Var::local("this", self->offset));
                me->setType(self->type);
                ExprPtr obj(new Unary('*', std::move(me)));
                obj->setType(self->type->pointee());
                return memberTemplateCall(std::move(obj), self->type->pointee(), component, qpos);
            }
            q += "::" + component;
        }
    }

    // **`S::f(...)` - a static member function, called with no object.** The
    // twin of the static *data* member found just above, and looked up the
    // same way.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("::")) {
        std::string q = peek().text;
        const Type *cls = findTypedef(q);
        std::string key;
        std::size_t consumed = 0;
        for (std::size_t k = 1; peekAt(k).is("::") &&
                                peekAt(k + 1).kind == TokenKind::Ident; k += 2) {
            const std::string component = peekAt(k + 1).text;
            // **Through the class each prefix names, as the static *data* member walk above
            // does.**
            std::string candidate = q + "::" + component;
            if (cls != nullptr && cls->isStructOrUnion()) {
                // [class.member.lookup]: a base declares it as well as the class does - `Tr::length(s)` for a
                // traits class derived from char_traits<char>.
                const Type *owner = findMemberOwner(cls, component);
                candidate = (owner != nullptr ? owner->tag() : cls->tag()) + "::" + component;
            }
            if (peekAt(k + 2).is("(") && hasStaticMemberNamed(candidate)) {
                key = candidate;
                consumed = k + 2;
            }
            // `S::f` not called - a static member function as a value, [expr.prim.general]/9.
            if (!peekAt(k + 2).is("(") && !peekAt(k + 2).is("::") && hasStaticMemberNamed(candidate)) {
                const std::size_t qpos = peek().pos;
                for (std::size_t i : *overloadsOf(candidate)) {
                    const Signature &f = functions_[i];
                    const Type *ownerType = findTypedef(f.owner);
                    if (f.access != Access::Public &&
                        (ownerType == nullptr || (!insideAccessOf(ownerType, f.access) && !isFriendOf(ownerType))))
                        src_.fail(qpos, "'" + candidate + "' is " +
                                        (f.access == Access::Private ? "private" : "protected") +
                                        " in '" + f.owner + "'");
                }
                at_ += k + 2;
                return functionAsValue(candidate, qpos);
            }
            q += "::" + component;
            cls = memberClassAlong(q, cls, component);
        }
        if (!key.empty()) {
            const std::size_t qpos = peek().pos;
            at_ += consumed;
            expect("(");
            std::vector<ExprPtr> args;
            parseArguments(args);
            const Signature &sig = resolveOverloadBraced(key, args, qpos);
            applyDefaults(sig, args, qpos);
            // A static member obeys access like any other - the check the
            // `.` and `->` paths make in memberCallWith, made here because
            // this call never goes through them.
            if (sig.access != Access::Public) {
                const Type *ownerType = findTypedef(sig.owner);
                if (ownerType == nullptr ||
                    (!insideAccessOf(ownerType, sig.access) &&
                     !isFriendOf(ownerType))) {
                    const char *how = sig.access == Access::Private
                                          ? "private" : "protected";
                    src_.fail(qpos, "'" + key + "' is " + how + " in '" +
                                    sig.owner + "' - it can be called only "
                                    "from inside the class");
                }
            }
            if (needsThis(sig))
                src_.fail(qpos, "'" + key + "' is not a static member function, "
                                "so it has to be called on an object - the "
                                "overload chosen here needs a 'this'");
            return completeCall(key, sig.symbol, nullptr, sig.returns,
                                sig.params, sig.variadic, qpos, std::move(args),
                                false);
        }
    }

    // **`Base::f(...)` inside a member - the version this class replaced.**
    // [expr.call]/1: naming the function with a qualified-id suppresses the
    // dispatch, which is the whole reason an override writes it.
    if (currentClass_ != nullptr && !inStaticMember_ &&
        peek().kind == TokenKind::Ident && peekAt(1).is("::")) {
        std::string q = peek().text;
        const Type *owner = nullptr;
        std::string member;
        std::size_t consumed = 0;
        for (std::size_t k = 1; peekAt(k).is("::") &&
                                peekAt(k + 1).kind == TokenKind::Ident; k += 2) {
            const std::string component = peekAt(k + 1).text;
            if (peekAt(k + 2).is("(")) {
                const Type *cls = findTypedef(q);
                if (cls == nullptr) cls = FindBase::named(currentClass_, q);
                if (cls != nullptr && cls->isStructOrUnion() &&
                    overloadsOf(cls->tag() + "::" + component) != nullptr) {
                    owner = cls;
                    member = component;
                    consumed = k + 2;
                }
            }
            q += "::" + component;
        }
        // `base::operator=(o)` - an operator function named through the class the same way.
        if (owner == nullptr) {
            std::size_t k = 1;
            while (peekAt(k).is("::") && peekAt(k + 1).kind == TokenKind::Ident) k += 2;
            if (peekAt(k).is("::") && peekAt(k + 1).is("operator")) {
                const Type *cls = findTypedef(q);
                if (cls == nullptr) cls = FindBase::named(currentClass_, q);
                if (cls != nullptr && cls->isStructOrUnion()) {
                    const std::size_t from = at_;
                    at_ += k + 1;
                    const std::string op = operatorName();
                    if (peek().is("(") && overloadsOf(cls->tag() + "::" + op) != nullptr) {
                        owner = cls;
                        member = op;
                        consumed = at_ - from;
                    }
                    at_ = from;
                }
            }
        }
        // Only a class this one *is* - itself or a base of it. Anything else is
        // some other class's member and has no object here to be called on.
        if (owner != nullptr &&
            currentClass_->unqualified() != owner->unqualified() &&
            accessibleBaseOffset(currentClass_, owner->unqualified()) < 0)
            owner = nullptr;
        if (owner != nullptr) {
            const std::size_t qpos = peek().pos;
            at_ += consumed;
            expect("(");
            std::vector<ExprPtr> args;
            parseArguments(args);
            const Local *held = findLocal("this");
            ExprPtr self(Var::local("this", held != nullptr ? held->offset
                                                            : thisOffset_));
            const Type *selfType = held != nullptr
                                       ? held->type
                                       : types_.pointerTo(currentClass_);
            self->setType(selfType);
            ExprPtr obj(new Unary('*', std::move(self)));
            obj->setType(selfType->pointee());
            return memberCallWith(std::move(obj), selfType->pointee(), member,
                                  qpos, std::move(args), owner);
        }
    }

    if (peek().kind == TokenKind::Ident) {
        std::string name = peek().text;
        std::size_t pos = peek().pos;

        const Local *l = findLocal(name);
        if (l != nullptr && l->unset) noteUnsetLocal(*l, pos);
        // A bound non-type template parameter hides a member and a namespace's name alike ([temp.local]/6).
        if (l == nullptr && boundValueParams_.count(name) != 0 && !peekAt(1).is("::"))
            if (const EnumConst *e = findEnum(name)) {
                at_++;
                if (e->paramType != nullptr && takesAddress(e->paramType))
                    return addressExpression(e->value, e->paramType, pos);
                ExprPtr n(new Num(e->value));
                n->setType(e->type != nullptr ? e->type : types_.intType());
                return n;
            }
        const GlobalSym *g = l != nullptr ? nullptr : findGlobal(name);
        // **Class scope comes before the namespace** - [basic.lookup.unqual]/8: a static member, a
        // member function or an enumerator of the class (or a base) hides a global of the same name.
        if (g != nullptr && classScopeDeclares(name)) g = nullptr;
        const Type *held = l != nullptr ? l->type : (g != nullptr ? g->type : nullptr);
        // A name that holds something callable rather than naming a function:
        // a function pointer, or an object whose `(` is [over.call].
        auto callable = [](const Type *t) {
            if (t != nullptr && t->isReference()) t = t->referent();
            return t != nullptr && (t->isFunctionPointer() || t->isFunction() ||
                                    t->unqualified()->isStructOrUnion());
        };
        bool callsThroughObject = callable(held);
        // **A data member holding something callable, called by its bare name**: [class.mfct.non-static]
        // makes `compare_(a, b)` `(*this).compare_(a, b)`, an ordinary call through what it holds, and
        // [basic.lookup.unqual] lets the member hide a namespace-scope function of that name.
        if (!callsThroughObject && l == nullptr && peekAt(1).is("(") && currentClass_ != nullptr) {
            ExprPtr outer = capturedThisPointer();
            const Type *roots[2] = { currentClass_,
                                     outer != nullptr ? outer->type()->pointee() : nullptr };
            for (const Type *root : roots)
                if (root != nullptr && !callsThroughObject)
                    if (const Member *m = root->findMember(name))
                        callsThroughObject = callable(m->type);
        }

        // **An unqualified static member** in a member function, the class
        // body or a lambda in either (`n` as an array bound), needing no
        // object; then enclosing classes' ([basic.lookup.unqual]/7).
        std::vector<const Type *> staticScopes;
        for (const Type *c = currentClass_ != nullptr ? currentClass_
                             : classStack_.empty() ? nullptr : classStack_.back();
             c != nullptr; c = c->enclosing())
            staticScopes.push_back(c);
        staticScopes.push_back(lambdaScope());
        for (const Type *staticScope : staticScopes) {
            if (l != nullptr || g != nullptr) break;
            if (staticScope == nullptr) continue;
            const Type::StaticMember *s = staticScope->findStaticMember(name);
            // Called, it is reached here only when it holds what is called.
            if (s != nullptr && peekAt(1).is("(") && !callable(s->type)) break;
            if (s != nullptr) {
                at_++;
                return staticMemberRef(staticScope, *s, staticScope->tag(), pos);
            }
        }

        // **A member function template called by its bare name** is `this->init(...)` too, and it
        // has no declaration to find until it is instantiated, so it is asked for as a template.
        // In the class's own body too - a static member's initialiser, the sizeof idiom's `sizeof(test<T>(0))`.
        const Type *enclosingClass = currentClass_ != nullptr ? currentClass_
                                   : classStack_.empty() ? nullptr : classStack_.back();
        if ((peekAt(1).is("(") || peekAt(1).is("<")) && !callsThroughObject && !blockUsingFunctions_.count(name) &&
            enclosingClass != nullptr && l == nullptr && g == nullptr) {
            const Type *owner = enclosingClass->unqualified();
            const Local *self = currentClass_ != nullptr ? findLocal("this") : nullptr;
            // Beside an ordinary member of the name, overload resolution below ranks the two.
            // A specialization of the template already made is listed under the name too, and is not one.
            bool alone = peekAt(1).is("<");
            if (!alone) {
                const Type *ordinary = findMemberOwner(owner, name);
                alone = true;
                if (ordinary != nullptr)
                    if (const std::vector<std::size_t> *set = overloadsOf(ordinary->unqualified()->tag() + "::" + name))
                        for (std::size_t k = 0; k < set->size(); k++)
                            if (!functions_[(*set)[k]].fromTemplate) alone = false;
            }
            // A base's, or a static one called from a static member function, where there is no `this`.
            if (memberTemplateOwner(owner, name) != nullptr && alone) {
                at_++;                         // the name; memberTemplateCall reads from the '<' or '('
                if (self == nullptr || inStaticMember_) return memberTemplateCall(nullptr, owner, name, pos);
                ExprPtr me(Var::local("this", self->offset));
                me->setType(self->type);
                ExprPtr obj(new Unary('*', std::move(me)));
                obj->setType(self->type->pointee());
                return memberTemplateCall(std::move(obj), self->type->pointee(), name, pos);
            }
        }

        // An unqualified call inside a member function looks for a member of this class first -
        // [class.mfct.non-static] makes `secret()` mean `this->secret()` - before the free-function
        // branch calls it undeclared. **A class's own name inside it is not a member function.**
        // In a member's declarator inside the class body - a trailing return type, an exception-specification -
        // the class is the one being defined, and an operand that is not evaluated may call its members.
        // (There currentClass_ is not this class - it is null, or still the class of the last member function
        // defined before it - so the class being defined is asked of classStack_.)
        const bool inClassBody = !classStack_.empty() && !classStack_.back()->isComplete();
        const Type *memberScope = inClassBody && unevaluated_ > 0 ? classStack_.back()->unqualified() : currentClass_;
        bool inherited = false;
        for (const Type *c = memberScope; c != nullptr; c = c->base()) {
            if (name == localOf(c->tag())) break;
            if (c != memberScope && inDependentBase(memberScope, c)) break;
            if (overloadsOf(c->tag() + "::" + name) != nullptr) { inherited = true; break; }
        }
        // **A static member called by its bare name, from inside the class.**
        // [class.static]/1 makes it `C::f(...)` and not `this->f(...)`: the
        // object is not merely unused, it is absent.
        // A friend's body written in its class looks there too, [basic.lookup.unqual]/9 (1.82: it did not, and
        // all three users of the trial of Setup 1.10.5 qualified their static helpers by hand).
        const Type *staticHome = currentClass_ != nullptr ? currentClass_ : friendScope_;
        if (peekAt(1).is("(") && !callsThroughObject && !blockUsingFunctions_.count(name) && staticHome != nullptr &&
            l == nullptr && g == nullptr) {
            std::string key;
            // Then the classes around it, [basic.lookup.unqual]/7, as the static data members above.
            std::vector<const Type *> roots(1, staticHome);
            if (!inherited)
                for (const Type *e = staticHome->enclosing(); e != nullptr; e = e->enclosing()) roots.push_back(e);
            roots.push_back(lambdaScope());
            for (const Type *root : roots)
                for (const Type *c = root; c != nullptr && key.empty(); c = c->base())
                    if (hasStaticMemberNamed(c->tag() + "::" + name))
                        key = c->tag() + "::" + name;
            if (!key.empty()) {
                at_ += 2;
                std::vector<ExprPtr> args;
                parseArguments(args);
                const Signature &sig = resolveOverloadBraced(key, args, pos);
                applyDefaults(sig, args, pos);
                // A static member obeys access like any other - the check the
                // `.` and `->` paths make in memberCallWith, made here because
                // this call never goes through them.
                if (sig.access != Access::Public) {
                    const Type *ownerType = findTypedef(sig.owner);
                    if (ownerType == nullptr ||
                        (!insideAccessOf(ownerType, sig.access) &&
                         !isFriendOf(ownerType))) {
                        const char *how = sig.access == Access::Private
                                                      ? "private" : "protected";
                        src_.fail(pos, "'" + key + "' is " + how + " in '" +
                                                sig.owner + "' - it can be called only "
                                                "from inside the class");
                    }
                }
                // One name holding both kinds is legal C++ and is refused here
                // rather than guessed at: the arguments are already read, so
                // there is no honest way back to the call that takes an object.
                if (needsThis(sig))
                    src_.fail(pos, "'" + name + "' names both a static and a "
                                   "non-static member here, and overload "
                                   "resolution chose the non-static one - "
                                   "which is not supported yet; call it on an "
                                   "object, or give the two different names");
                return completeCall(key, sig.symbol, nullptr, sig.returns,
                                    sig.params, sig.variadic, pos,
                                    std::move(args), false);
            }
        }

        if (peekAt(1).is("(") && !callsThroughObject && !blockUsingFunctions_.count(name) && memberScope != nullptr &&
            l == nullptr && g == nullptr && inherited) {
            const Local *self = inClassBody && unevaluated_ > 0 ? nullptr : findLocal("this");
            // A held body's declarator is read again before its function begins, and the `this` then standing
            // is the last function's - another class's, as likely as not. Only this class's own is the object.
            if (self != nullptr && (self->type == nullptr || !self->type->isPointer() ||
                                    self->type->pointee()->unqualified() != memberScope->unqualified()))
                self = nullptr;
            // [expr.prim.general]/3: in a member function's declarator `this` is there too, for an operand
            // that is not evaluated - `noexcept(noexcept(helper()))`, `-> decltype(helper())` - as the
            // `this` expression has it (1.80: the bare call was "not declared" where `this->helper()` was read).
            if (self != nullptr || (unevaluated_ > 0 && !inStaticMember_)) {
                at_ += 2;
                const Type *selfType = self != nullptr ? self->type : types_.pointerTo(memberScope);
                ExprPtr me(Var::local("this", self != nullptr ? self->offset : thisOffset_));
                me->setType(selfType);
                ExprPtr obj(new Unary('*', std::move(me)));
                obj->setType(selfType->pointee());
                return memberCall(std::move(obj), selfType->pointee(), name, pos);
            }
        }

        // `P(1)` where P names a class: a temporary, not a call to a function
        // nobody declared. Asked before the call branch below, which would
        // look the name up in the function table and report it undeclared.
        if (peekAt(1).is("{") && !callsThroughObject &&
            l == nullptr && g == nullptr) {
            if (const Type *named = findTypedef(name)) {
                at_++;
                return bracedValueInit(named, pos);
            }
        }

        if (peekAt(1).is("(") && !callsThroughObject && l == nullptr &&
            g == nullptr && !localHidesType()) {
            const Type *named = findTypedef(name);
            if (named != nullptr && named->isStructOrUnion()) {
                at_ += 2;
                return classTemporary(named, pos);
            }
            // A typedef of anything else, or an enumeration: `I()` and
            // `Color(1)` are the fundamental forms above under another name.
            if (named != nullptr) {
                at_++;
                return functionalCast(named, pos);
            }
        }

        // A *member function* of that class, called by its bare name. The branch
        // further up asks `currentClass_`, which inside the call operator is the
        // closure, so it finds nothing and the name is reported undeclared.
        if (peekAt(1).is("(") && l == nullptr && g == nullptr) {
            if (ExprPtr outer = capturedThisPointer()) {
                const Type *of = outer->type()->pointee();
                bool has = false;
                for (const Type *c = of; c != nullptr; c = c->base())
                    if (overloadsOf(c->tag() + "::" + name) != nullptr) {
                        has = true;
                        break;
                    }
                if (has) {
                    at_ += 2;                  // the name and its '('
                    ExprPtr obj(new Unary('*', std::move(outer)));
                    obj->setType(of);
                    return memberCall(std::move(obj), of, name, pos);
                }
            }
        }

        if (peekAt(1).is("(") && !callsThroughObject) {
            at_ += 2;
            // The arguments first, then the function: with a set to choose
            // from there is nothing to convert them to until one is chosen.
            std::vector<ExprPtr> args;
            parseArguments(args);
            const Signature &sig = resolveOverloadBraced(name, args, pos);
            applyDefaults(sig, args, pos);
            return completeCall(name, sig.symbol, nullptr, sig.returns, sig.params,
                                sig.variadic, pos, std::move(args),
                                !sig.owner.empty());
        }

        at_++;
        // **[basic.lookup.unqual]/1 and [basic.scope.hiding]/1**.
        if (ExprPtr v = localRef(name)) return v;

        // Class scope.
        if (currentClass_ != nullptr) {
            const Local *self = findLocal("this");
            const Member *m = currentClass_->findMember(name);
            // [temp.dep]/3: a base that depends on a template parameter is not looked in.
            if (m != nullptr && m->declaredIn != nullptr && inDependentBase(currentClass_, m->declaredIn)) {
                if (g == nullptr)
                    src_.fail(pos, "'" + name + "' is a member of '" + m->declaredIn->describe() + "', a base "
                                   "that depends on a template parameter, and [temp.dep]/3 leaves such a base "
                                   "out of unqualified lookup - write 'this->" + name + "'");
                m = nullptr;
            }
            if (m != nullptr) {
                if (m->ambiguous) refuseAmbiguousMember(currentClass_, *m, pos);
                // **[class.access]/1 applies however the member is spelled**, and this path checked
                // nothing: `x` inside a derived class reached a private member of its base where
                // `this->x` and `d.x` were both refused.
                checkAccessible(currentClass_, *m, pos);
                if (self == nullptr && unevaluated_ > 0)
                    return memberWithoutObject(currentClass_, *m, pos);
                if (self == nullptr)
                    src_.fail(pos, "'" + name + "' is a member and there is no "
                                   "object here to read it from");
                const Type *held = self->type->pointee();
                ExprPtr acc = thisMember(self->offset, held, *m);
                // **A member of a virtual base is not at a constant offset
                // from `this` either**: the same walk through the vtable the
                // `.` and `->` paths take, from `*this`.
                if (m->inVirtualBase != nullptr) {
                    refuseVirtualBaseMember(held, *m, name, pos);
                    ExprPtr me(Var::local("this", self->offset));
                    me->setType(self->type);
                    ExprPtr obj(new Unary('*', std::move(me)));
                    obj->setType(held);
                    if (ExprPtr viaVb = virtualBaseMember(std::move(obj), held, *m))
                        acc = std::move(viaVb);
                }
                // The same two rules as the `.` and `->` paths: a const
                // object does not reach through a reference member, and a
                // reference member is read by dereferencing what it holds.
                acc->setType(memberTypeIn(held, *m));
                return useReference(std::move(acc));
            }

            // **A member of the class the lambda was written in**, reached through
            // the captured pointer. Inside the call operator `currentClass_` is the
            // closure, so the search above looked in the wrong class entirely.
            if (ExprPtr outer = capturedThisPointer()) {
                const Type *of = outer->type()->pointee();
                if (const Member *om = of->findMember(name)) {
                    checkAccessible(of, *om, pos);
                    ExprPtr obj(new Unary('*', std::move(outer)));
                    obj->setType(of);
                    ExprPtr acc(new MemberAccess(std::move(obj), name,
                                                 om->offset, om->width,
                                                 om->bitOffset));
                    // The same two rules the three ordinary member paths
                    // follow, which this one did not: a const object reaches
                    // its members as const, a reference or `mutable` apart.
                    acc->setType(memberTypeIn(of, *om));
                    return useReference(std::move(acc));
                }
            }

            if (const EnumConst *e = findClassEnum(name)) {
                ExprPtr n(new Num(e->value));
                n->setType(e->type != nullptr ? e->type : types_.intType());
                return n;
            }
        }

        if (ExprPtr f = memberFunctionByName(name, pos)) return f;

        // Namespace scope, last of the three.
        if (const EnumConst *e = findEnum(name)) {
            ExprPtr n(new Num(e->value));
            n->setType(e->type != nullptr ? e->type : types_.intType());
            return n;
        }
        if (ExprPtr v = globalRef(name)) return v;

        // Taking the address of an overloaded name needs a target type to
        // choose by - [over.over] - and there is none here.
        if (ExprPtr f = functionAsValue(
                qualifyForLookup(name, &Parser::hasFunctionNamed), pos))
            return f;
        // `S{...}` with a variable or function S in scope: the name hides the type, [basic.scope.hiding]/2.
        if (peek().is("{") && findTypedef(name) != nullptr)
            src_.fail(pos, "'" + name + "' names an object or function here, which hides the type "
                           "of that name, so '" + name + "{...}' is no temporary");
        // `S::x` reached here found no static member, enumerator or function: say which it was.
        // The class is the longest prefix naming one, `n::S::nope` reaching S through n.
        std::size_t k = 0;
        const Type *cls = findTypedef(name);
        for (std::string q = name; peekAt(k).is("::") && peekAt(k + 1).kind == TokenKind::Ident &&
                                   peekAt(k + 2).is("::"); k += 2) {
            q += "::" + peekAt(k + 1).text;
            const Type *next = findTypedef(q);
            if (next == nullptr && cls != nullptr && cls->isStructOrUnion())
                next = lookupInClass(cls, peekAt(k + 1).text);
            if (next == nullptr && cls != nullptr) break;
            cls = next;                        // a namespace leaves it null for the next step
        }
        if (cls != nullptr && peekAt(k).is("::") && peekAt(k + 1).kind == TokenKind::Ident)
            if (cls->isStructOrUnion()) {
                const std::string member = peekAt(k + 1).text;
                if (const Member *m = cls->findMember(member)) {
                    if (ExprPtr mine = qualifiedMemberThroughThis(cls, *m, member, pos)) {
                        at_ += k + 2;
                        return mine;
                    }
                    src_.fail(peekAt(k + 1).pos, "'" + member + "' is a non-static data member "
                              "of '" + cls->tag() + "' and needs an object here - only "
                              "sizeof and decltype may name it with none");
                }
                src_.fail(peekAt(k + 1).pos, "'" + cls->tag() + "' has no member called '" +
                                         member + "'");
            }
        // [temp.variadic]/5: a pack is written with the `...` that expands it, never bare.
        if (packs_.count(name) != 0)
            src_.fail(pos, "'" + name + "' is a parameter pack, and here it is written without the "
                           "'...' that expands it");
        // [dcl.fct.def.general]/8: `__func__` inside a function is as if `static const char
        // __func__[] = "name";` opened its body - the unqualified name, `m` for `S::m`, which is
        // cl's spelling too (1.27, 2026-10-21, review of 1.25, item 3: it was "not declared").
        if (name == "__func__" && !currentFunctionName_.empty()) {
            std::string bare = currentFunctionName_;
            const std::size_t cut = bare.rfind("::");
            if (cut != std::string::npos) bare = bare.substr(cut + 2);
            const std::string label = ".L.str." + std::to_string(strings_++);
            program->strings.push_back(StringLit{ label, bare + '\0', 1 });
            ExprPtr n(new StrLit(label, bare));
            n->setType(types_.arrayOf(types_.withConst(types_.charType()),
                                      static_cast<long long>(bare.size()) + 1));
            return n;
        }
        src_.fail(pos, "'" + name + "' was not declared");
    }

    src_.fail(peek().pos, "expected an expression");
}

// `x`, `p.q`, `p->q->r` and nothing else - what [dcl.type.simple] calls an
// id-expression or a class member access, which answers with a declared type. One
// pair of parentheses changes the answer, so this reads tokens and not the tree.
bool Parser::atNamePath() const {
    if (peek().kind != TokenKind::Ident) return false;
    std::size_t k = 1;
    for (;;) {
        // `S::m` and `C<int>::m` are id-expressions too: a class template's arguments are stepped over.
        if (peekAt(k).is("<") && isClassTemplate(peekAt(k - 1).text)) {
            int depth = 0;
            for (;; k++) {
                const Token &t = peekAt(k);
                if (t.kind == TokenKind::End || t.is(";")) return false;
                if (depth > 0 && (t.is("(") || t.is("["))) { k = groupCloseAt(at_ + k) - at_; continue; }
                if (t.is("<")) depth++;
                else if (t.is(">")) { if (--depth == 0) { k++; break; } }
                else if (t.is(">>")) { depth -= 2; if (depth <= 0) { k++; break; } }
            }
            if (!peekAt(k).is("::")) return false;
        }
        if (peekAt(k).is(")")) return true;
        if (!peekAt(k).is(".") && !peekAt(k).is("->") && !peekAt(k).is("::")) return false;
        if (peekAt(k + 1).kind != TokenKind::Ident) return false;
        k += 2;
    }
}

const Type *Parser::decltypeSpecifier() {
    const std::size_t pos = peek().pos;
    at_++;
    expect("(");
    // **`decltype(auto)` is C++14 and `decltype(e)` is C++11**, and the two are told
    // apart by one token. Named here because the C++11 form is built: without it the
    // `auto` reads as the start of an expression and the error is useless.
    if (peek().is("auto"))
        src_.fail(peek().pos, "'decltype(auto)' is C++14, and this compiler is "
                              "C++11 - 'decltype' of an expression works, and "
                              "'auto' on its own deduces from an initialiser");
    // **In a pattern, `decltype` of an expression over the parameters is a type spelled by the
    // expression**: `Dt` for an unparenthesised id-expression or member access, `DT` else -
    // measured, `-> decltype(t.v)` is `Dtdtfp_1vE` and `-> decltype((t))` `DTfp_E`.
    if (patternOnly_ && dependentAhead(false)) {
        const std::size_t from = at_;
        DepExpr tree = *dependentExpression(false, true);
        const std::size_t to = at_;
        expect(")");
        bool wrapped = tokens_[from].is("(");
        for (std::size_t i = from, depth = 0; wrapped && i < to; i++) {
            if (tokens_[i].is("(")) depth++;
            else if (tokens_[i].is(")") && --depth == 0 && i + 1 != to) wrapped = false;
        }
        const DepExpr::Op top = tree.op;
        const bool id = !wrapped && (top == DepExpr::TemplateParam ||
                                     top == DepExpr::FunctionParam ||
                                     top == DepExpr::QualifiedParamName ||
                                     top == DepExpr::Member || top == DepExpr::Arrow);
        tree.text = depText(from, to);
        return types_.dependentExpr(types_.intern(tree), id);
    }
    const bool namePath = atNamePath();

    // **A name on its own is looked up, not evaluated.** A reference variable is the
    // case that needs it: every mention is lowered to a dereference, so `r` has type
    // T, and `decltype(r)` has to answer what the declaration said.
    if (namePath && peekAt(1).is(")")) {
        const std::string name = peek().text;
        const Type *declared = nullptr;
        if (const Local *l = findLocal(name)) declared = l->type;
        else if (const GlobalSym *g = findGlobal(name)) declared = g->type;
        if (declared != nullptr) {
            at_ += 2;
            return declared;
        }
    }
    // `decltype(S::r)` for a static member answers what the class declared - `int &` for a reference.
    if (namePath && peekAt(1).is("::")) {
        std::string q = peek().text;
        for (std::size_t k = 1; peekAt(k).is("::") && peekAt(k + 1).kind == TokenKind::Ident;
             k += 2) {
            const Type *cls = findTypedef(q);
            if (cls != nullptr && cls->isStructOrUnion() && peekAt(k + 2).is(")"))
                if (const Type::StaticMember *sm = cls->findStaticMember(peekAt(k + 1).text)) {
                    at_ += k + 3;
                    return sm->type;
                }
            q += "::" + peekAt(k + 1).text;
        }
    }

    // Unevaluated: [dcl.type.simple]/4. The operand is read for its type and
    // nothing in it happens, so nothing it registered may outlive the read.
    Discarded held(this);
    Unevaluated unevaluated(this);
    NotCounted notCounted(this);
    const std::size_t from = at_;
    // Inside `decltype(...)` a `>` is an operator again, as inside parentheses: `Same<decltype(a > b),
    // bool>` read its `>` as the template-id's close (1.27, 2026-10-21, review of 1.25, item 3).
    const bool wasInArgs = inTemplateArgs_;
    inTemplateArgs_ = false;
    ExprPtr e = expr();
    inTemplateArgs_ = wasInArgs;
    const std::size_t to = at_;
    expect(")");
    const Type *t = e->type();
    if (t == nullptr)
        src_.fail(pos, "'decltype' needs an expression with a type, and this "
                       "one has none");
    // **An unparenthesised member access answers the declared type**
    // whatever its object (`decltype(T().v)` is double; `decltype(cs.m)` int,
    // no const): the record's where name and offset match, else the access's.
    if (to >= from + 3 && tokens_[to - 1].kind == TokenKind::Ident &&
        (tokens_[to - 2].is(".") || tokens_[to - 2].is("->"))) {
        // A reference member is read through the address it holds, `*(s.r)`.
        const Expr *access = e.get();
        const Unary *through = dynamic_cast<const Unary *>(access);
        if (through != nullptr && through->op() == '*') access = &through->operand();
        if (const MemberAccess *ma = dynamic_cast<const MemberAccess *>(access)) {
            const Type *cls = ma->object().type() != nullptr ? ma->object().type()->unqualified() : nullptr;
            if (cls != nullptr && cls->isStructOrUnion())
                if (const Member *m = cls->findMember(ma->name()))
                    if (!m->ambiguous && m->offset == ma->offset() && m->type != nullptr &&
                        (access == e.get() || m->type->isReference()))
                        return m->type;
            if (access == e.get()) return t;
        }
    }
    // [dcl.type.simple]/4 asks of the function a name names, not of the pointer it decays to.
    std::size_t opened = from;
    while (tokens_[opened].is("(")) opened++;
    if (!tokens_[opened].is("&") && functionDesignated(*e) != nullptr && t->isPointer())
        return namePath ? t->pointee() : types_.referenceTo(t->pointee());
    if (t->isFunction() && !namePath) return types_.referenceTo(t);
    // `*pf` is pf itself as far as the tree goes, and a function lvalue all the same.
    if (tokens_[opened].is("*") && t->isPointer() && t->pointee()->isFunction() &&
        dynamic_cast<const Var *>(e.get()) != nullptr)
        return types_.referenceTo(t->pointee());
    if (namePath) return t;
    // [dcl.type.simple]/4: an xvalue answers `T &&`, an lvalue `T &`, a prvalue `T` - without cv where
    // T is not a class or an array, [expr]/6. A temporary is marked an xvalue so it binds to `T &&` and
    // is a prvalue all the same, so the prvalue mark is asked first; the xvalue mark before the lvalue.
    const Type *prvalue = t->isStructOrUnion() || t->isArray() ? t : t->unqualified();
    if (e->declCategory() == Expr::Category::Prvalue) return prvalue;
    if (e->isXvalue()) return types_.rvalueReferenceTo(t);
    if (e->declCategory() == Expr::Category::Lvalue) return types_.referenceTo(t);
    // [lex.string]/8: a string literal is an lvalue, so `decltype("ab")` is `const char (&)[3]` (1.27,
    // 2026-10-21, review of 1.25, item 3 - the array came back without the reference).
    if (dynamic_cast<const StrLit *>(e.get()) != nullptr) return types_.referenceTo(t);
    return isLvalue(*e) ? types_.referenceTo(t) : prvalue;
}

// A reference is used by going through the address in its slot, and every use does
// it. The parser hands back a dereference, so assignment, address-of and member
// access see an ordinary lvalue - the trade `(bool)x` becoming `x != 0` is.
ExprPtr Parser::useReference(ExprPtr e) {
    if (!e->type()->isReference()) return e;
    const Type *referent = e->type()->referent();
    e->setType(types_.pointerTo(referent));
    ExprPtr deref(new Unary('*', std::move(e)));
    deref->setType(referent);
    return deref;
}

// [expr.ref]/4: a member of a cv object has the cv-union of the object's and its own - const unless it
// is mutable, volatile whatever it is; a reference member is its referent's type alone.
const Type *Parser::memberTypeIn(const Type *obj, const Member &m) const {
    if (m.type->isReference()) return m.type;
    const Type *t = obj->isConst() && !m.isMutable ? types_.withConst(m.type) : m.type;
    return obj->isVolatile() ? types_.withVolatile(t) : t;
}
// [dcl.init.ref]/5: a reference binds an lvalue whose cv it keeps or adds to, never one it would drop.
void Parser::refuseDroppedQualifier(const Type *it, const Type *referent, const Type *ref,
                                    const std::string &what, std::size_t pos) const {
    if (it->isConst() && !referent->isConst())
        src_.fail(pos, what + " is '" + ref->describe() + "' and this is '" +
                       it->describe() + "' - a reference that can write "
                       "cannot bind to a const");
    if (it->isVolatile() && !referent->isVolatile())
        src_.fail(pos, what + " is '" + ref->describe() + "' and this is '" +
                       it->describe() + "' - a reference to a non-volatile type "
                       "cannot bind to a volatile object, whose every access is the program's");
}

// What is stored in a reference is an address, so binding one is taking the
// address of the initialiser. The rules here are the whole of what makes a
// reference different from a pointer that is always dereferenced.
ExprPtr Parser::bindReference(const Type *ref, ExprPtr init, std::size_t pos,
                              const std::string &what) {
    const Type *referent = ref->referent();
    const Type *it = init->type();

    // **A reference to a function holds the function's address** - [dcl.init.ref]/5 binds it to a
    // function lvalue of the same type, and `&f` already is that address.
    if (referent->isFunction()) {
        const Type *fn = functionDesignated(*init);
        if (fn == nullptr || fn->unqualified() != referent->unqualified())
            src_.fail(pos, what + " is '" + ref->describe() + "' and this is '" +
                           it->describe() + "' - a reference to a function binds "
                           "only a function of that type");
        if (it->isFunction()) {
            init.reset(new Unary('&', std::move(init)));
        }
        init->setType(types_.pointerTo(referent));
        return init;
    }

    // **An assignment and a prefix `++` are lvalues naming their left operand** ([expr.ass]/1): bound
    // as `*(a = b, &a)`, the operand named again - which needs it free of effects to be named twice.
    if (init->declCategory() == Expr::Category::Lvalue) {
        const Assign *as = dynamic_cast<const Assign *>(init.get());
        // `int &r = (a = b) = c;` - the last of the assignments names the object
        if (as == nullptr)
            if (const Comma *c = dynamic_cast<const Comma *>(init.get())) as = dynamic_cast<const Assign *>(&c->right());
        if (as != nullptr)
            if (ExprPtr again = clonePure(as->target())) {
                const Type *obj = as->target().type();
                ExprPtr at(new Unary('&', std::move(again)));
                at->setType(types_.pointerTo(obj));
                ExprPtr both(new Comma(std::move(init), std::move(at)));
                both->setType(types_.pointerTo(obj));
                init.reset(new Unary('*', std::move(both)));
                init->setType(obj);
                it = obj;
            }
    }

    // Binding takes an address, so the two things that have none cannot be bound to
    // directly. A const reference still may: it copies them into a temporary below,
    // which is what the standard says happens.
    const char *noAddressBecause = nullptr;
    std::string noAddressName;
    if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(init.get()))
        if (m->isBitField()) {
            noAddressBecause = "a bit-field";
            noAddressName = m->name();
        }
    if (const Var *v = dynamic_cast<const Var *>(init.get()))
        if (v->noAddress()) {
            noAddressBecause = "register";
            noAddressName = v->name();
        }

    // **An rvalue reference takes exactly what an lvalue one will not**
    // ([dcl.init.ref]): a value with nowhere to live, not an object; an
    // lvalue of another type that converts makes such a value first (/5).
    const bool sameKind = it->unqualified() == referent->unqualified() ||
                          accessibleBaseOffset(it, referent) > -1 ||
                          (it->isArray() && referent->isArray() && it->sameArrayShape(referent));
    if (ref->isRValueReference() && isLvalue(*init) && sameKind)
        src_.fail(pos, what + " is '" + ref->describe() + "' and this is an "
                       "object with an address of its own - an rvalue "
                       "reference binds only to a value that has none, so "
                       "that taking it apart harms nobody");

    // The direct binding: an addressable glvalue of the type named. A string
    // literal is an lvalue array ([expr.prim.general]/1): `const char (&)[3]`
    // from "cd" holds the literal's own address.
    if (referent->isArray() && it->isArray() && dynamic_cast<const StrLit *>(init.get()) != nullptr &&
        it->sameArrayShape(referent) &&
        it->innermostElement()->unqualified() == referent->innermostElement()->unqualified() &&
        referent->innermostElement()->isConst() && !ref->isRValueReference()) {
        ExprPtr first = decay(std::move(init));
        ExprPtr at(new Cast(types_.pointerTo(referent), std::move(first)));
        at->setType(types_.pointerTo(referent));
        return at;
    }

    // [dcl.init.ref]/5: an lvalue reference to other than a non-volatile const binds an lvalue and nothing else.
    if (!ref->isRValueReference() && (!referent->isConst() || referent->isVolatile()) && sameKind &&
        (init->isXvalue() || init->declCategory() == Expr::Category::Prvalue))
        src_.fail(pos, what + " is '" + ref->describe() + "' and this is an rvalue - an lvalue reference "
                       "that is not to a non-volatile const binds an lvalue alone, [dcl.init.ref]/5");

    // An xvalue binds `B &&` to its base subobject as an lvalue binds `B &` - [dcl.init.ref]/5: `base(std::move(o))`
    // in a derived move constructor moves o's own base, not a copy of it. An lvalue was refused above.
    if (isGlvalue(*init) && noAddressBecause == nullptr &&
        it->unqualified() != referent->unqualified() &&
        accessibleBaseOffset(it, referent) > -1) {
        refuseDroppedQualifier(it, referent, ref, what, pos);
        ExprPtr addr(new Unary('&', std::move(init)));
        addr->setType(types_.pointerTo(it->unqualified()));
        return convert(std::move(addr), types_.pointerTo(referent));
    }

    // **A reference to an array binds it directly**, the element gaining const
    // at most - `const double (&)[N]` takes a `double[N]` lvalue.
    const bool arrayBind = it->isArray() && referent->isArray() &&
        it->sameArrayShape(referent) &&
        it->innermostElement()->unqualified() ==
            referent->innermostElement()->unqualified() &&
        (referent->innermostElement()->isConst() ||
         !it->innermostElement()->isConst());

    if (isGlvalue(*init) && noAddressBecause == nullptr &&
        (it->unqualified() == referent->unqualified() || arrayBind)) {
        if (!arrayBind) refuseDroppedQualifier(it, referent, ref, what, pos);
        ExprPtr addr(new Unary('&', std::move(init)));
        addr->setType(types_.pointerTo(referent));
        return addr;
    }

    // [dcl.init.ref]/5: a class that converts to an lvalue the reference binds directly - a conversion function
    // returning `U &`, U the referent's type and no more qualified than it - is bound to what that function
    // returns: `int &r = std::ref(n);`, which is what reference_wrapper is for, and make_tuple(std::ref(n)).
    // (Until 1.75 a reference to non-const refused it, and one to const took a copy.)
    if (!ref->isRValueReference() && it->unqualified()->isStructOrUnion() && !referent->isArray()) {
        const Signature *via = referenceConversion(it, referent);
        if (via != nullptr) {
            const Type *object = it;
            const std::string name = via->name;
            std::vector<ExprPtr> none;
            ExprPtr got = memberCallWith(std::move(init), object, name, pos, std::move(none));
            return bindReference(ref, std::move(got), pos, what);
        }
    }

    // Anything else needs a temporary to bind to, and only a const reference may
    // have one - [dcl.init.ref]/5, a non-volatile one. The refusals are separate: a type
    // that does not match, and a value with no address. An rvalue reference is allowed one.
    if (referent->isConst() && referent->isVolatile() && !ref->isRValueReference())
        src_.fail(pos, what + " is '" + ref->describe() + "', and this would have to be a temporary - "
                       "[dcl.init.ref]/5 binds one to a reference to non-volatile const alone");
    if (!referent->isConst() && !ref->isRValueReference()) {
        if (noAddressBecause != nullptr)
            src_.fail(pos, "'" + noAddressName + "' is " + noAddressBecause +
                           ", and has no address for a reference to hold - a "
                           "'const " + referent->unqualified()->describe() +
                           " &' would take a copy of it instead");
        // **A `?:` whose arms are lvalues of one type is an lvalue and binds**
        // - [expr.cond]/4, and it is lowered to `*(c ? &a : &b)`, which
        // reaches here as a dereference rather than a `Conditional`.
        if (dynamic_cast<const Conditional *>(init.get()) != nullptr)
            src_.fail(pos, "the arms of this '?:' are not both lvalues of one "
                           "type, so [expr.cond] makes it a value rather than "
                           "an object - and a value has no address for a "
                           "reference to bind to");
        if (isLvalue(*init))
            src_.fail(pos, what + " is '" + ref->describe() + "' and this is '" +
                           it->describe() + "' - a reference binds to the type "
                           "it names, and nothing is converted on the way in; a "
                           "'const " + referent->unqualified()->describe() +
                           " &' would take a converted copy");
        src_.fail(pos, what + " is '" + ref->describe() + "', and this is a "
                       "value with no address to bind to - a 'const " +
                       referent->unqualified()->describe() + " &' would take a "
                       "copy of it instead");
    }

    // **A class a call returns is an object of this frame already** (the
    // callee's result slot): the reference binds to it, not a copy, and it
    // is what extendTemporary keeps or the full-expression ends (/5).
    if (it->unqualified()->isStructOrUnion() && referent->unqualified()->isStructOrUnion() &&
        (it->unqualified() == referent->unqualified() || accessibleBaseOffset(it, referent) > -1))
        if (const Call *c = resultCall(init.get()))
            if (c->resultSlot() != 0) {
                const Type *whole = it->unqualified();
                ExprPtr held(Var::local("$ret", c->resultSlot()));
                held->setType(whole);
                ExprPtr addr(new Unary('&', std::move(held)));
                addr->setType(types_.pointerTo(whole));
                ExprPtr at = convert(std::move(addr), types_.pointerTo(referent));
                const Type *atType = at->type();
                ExprPtr both(new Comma(std::move(init), std::move(at)));
                both->setType(atType);
                return both;
            }

    // **A derived class value bound by a const reference to a base**, `f(make())` for `f(const B &)`:
    // [dcl.init.ref]/5 binds the reference to the base subobject of the temporary, so the temporary
    // holds the whole D and the address handed over is its B's.
    if (it->unqualified()->isStructOrUnion() && referent->unqualified()->isStructOrUnion() &&
        it->unqualified() != referent->unqualified() && accessibleBaseOffset(it, referent) > -1) {
        const Type *whole = it->unqualified();
        const int wslot = allocateFrameSlot(whole);
        const std::string wtemp = ".ref" + std::to_string(refTemps_++);
        ExprPtr target(Var::local(wtemp, wslot));
        target->setType(whole);
        ExprPtr keep(new Assign(std::move(target), std::move(init)));
        keep->setType(whole);
        ExprPtr held(Var::local(wtemp, wslot));
        held->setType(whole);
        ExprPtr addr(new Unary('&', std::move(held)));
        addr->setType(types_.pointerTo(whole));
        ExprPtr base = convert(std::move(addr), types_.pointerTo(referent));
        const Type *baseType = base->type();
        ExprPtr both(new Comma(std::move(keep), std::move(base)));
        both->setType(baseType);
        return both;
    }
    // [dcl.init.ref]/5 with [conv.array] and [conv.func]: the temporary the reference binds is copy-initialised
    // from the expression, so an array or a function given for a reference to a pointer decays first -
    // `void f(const char *const &); f("s");`, and with it `std::tuple<int, const char *> t(1, "s");` (1.79:
    // "is 'const char * const' and this is 'const char [2]'").
    if (referent->isPointer() && init->type() != nullptr &&
        (init->type()->isArray() || init->type()->isFunction()))
        init = decay(std::move(init));
    checkAssignable(*init, referent, pos, what);
    const Type *store = referent->unqualified();
    const Type *addrType = types_.pointerTo(referent);
    int slot = allocateFrameSlot(store);
    std::string temp = ".ref" + std::to_string(refTemps_++);

    ExprPtr target(Var::local(temp, slot));
    target->setType(store);
    ExprPtr keep(new Assign(std::move(target), convert(std::move(init), store)));
    keep->setType(store);

    ExprPtr held(Var::local(temp, slot));
    held->setType(store);
    ExprPtr addr(new Unary('&', std::move(held)));
    addr->setType(addrType);

    ExprPtr both(new Comma(std::move(keep), std::move(addr)));
    both->setType(addrType);
    return both;  // not registered for destruction, deliberately
}

// **Block scope and namespace scope are separate questions**, because [basic.lookup.unqual] puts
// class scope between them: a member hides a global and is hidden by a local. objectRef asks both
// in order for the callers that are not inside a class and have no middle step to take.
ExprPtr Parser::objectRef(const std::string &name) {
    if (ExprPtr v = localRef(name)) return v;
    return globalRef(name);
}

// An ordinary member function of this name, beside which a member template is ranked rather
// than taken ([over.match.best]); a made specialization is not one.
bool Parser::hasOrdinaryMember(const Type *cls, const std::string &name) {
    const Type *ordinary = findMemberOwner(cls->unqualified(), name);
    if (ordinary == nullptr) return false;
    if (const std::vector<std::size_t> *set = overloadsOf(ordinary->unqualified()->tag() + "::" + name))
        for (std::size_t k = 0; k < set->size(); k++)
            if (!functions_[(*set)[k]].fromTemplate) return true;
    return false;
}

// Does class scope, between block scope and the namespace, declare this name - a data member, a
// static one, a member function or an enumerator of the class being read, a base, or the class a
// lambda was written in. Asked of a name a global also answers, which the class then hides.
bool Parser::classScopeDeclares(const std::string &name) {
    if (boundValueParams_.count(name) != 0) return false;
    const Type *roots[2] = { currentClass_ != nullptr ? currentClass_
                                                      : classStack_.empty() ? nullptr
                                                                            : classStack_.back(),
                             lambdaScope() };
    for (const Type *root : roots) {
        if (root == nullptr) continue;
        // Not through a base that depends on a template parameter - [temp.dep]/3.
        const Member *m = root->findMember(name);
        if (m != nullptr && m->declaredIn != nullptr && inDependentBase(root, m->declaredIn)) m = nullptr;
        if (m != nullptr || root->findStaticMember(name) != nullptr)
            return true;
        for (const Type *c = root; c != nullptr; c = c->base()) {
            if (c != root && inDependentBase(root, c)) break;
            if (name != localOf(c->tag()) && overloadsOf(c->tag() + "::" + name) != nullptr)
                return true;
            // A member function template too: it has no entry among the overloads until one is asked for, and a
            // global variable of the name took its place - a program's `bool go;` beside a class's `template
            // <class T> int go(T)` made `go(4)` in a member "not declared", and <future>'s packaged_task has
            // such a member (1.76).
            if (name != localOf(c->tag()) && isMemberTemplate(c, name)) return true;
        }
    }
    return findClassEnum(name) != nullptr;
}

ExprPtr Parser::localRef(const std::string &name) {
    if (const Local *l = findLocal(name)) {
        // [expr.prim.lambda]/12: a const local of the enclosing function, read in a lambda for its
        // value, is no odr-use and needs no capture - its value stands in (1.27, 2026-10-21, review of
        // 1.25, item 3: `[] { return k + 1; }` found no `k`).
        if (l->constantOnly) {
            ExprPtr n(new Num(l->constantValue));
            n->setType(l->type);
            return n;
        }
        Var *v = l->staticName.empty() ? Var::local(name, l->offset)
                                       : Var::global(l->staticName);
        v->setReadOnly(l->isConst);
        v->setNoAddress(l->isRegister);
        // A block-scope thread_local is reached directly; an `extern` one through its wrapper.
        if (l->isThreadLocal) tlsVar(v, l->tlsViaWrapper, l->tlsViaWrapper);
        ExprPtr n(v);
        n->setType(l->type);
        return useReference(std::move(n));
    }
    return nullptr;
}

ExprPtr Parser::globalRef(const std::string &name) {
    if (const GlobalSym *g = findGlobal(name)) {
        Var *v = Var::global(name);
        v->setSymbol(g->symbol);
        v->setReadOnly(g->isConst);
        if (g->isThreadLocal) tlsVar(v, g->tlsViaWrapper, g->tlsExternal);
        ExprPtr n(v);
        n->setType(g->type);
        // A file-scope reference, like a local one, is a slot of its address
        return useReference(std::move(n));
    }
    return nullptr;
}

// A25: the member is in two base subobjects, and cl's C2385 is the answer.
void Parser::refuseAmbiguousMember(const Type *cls, const Member &m,
                                   std::size_t pos) {
    src_.fail(pos, "'" + m.name + "' is ambiguous in '" + cls->describe() +
                   "': more than one of its bases has a member of that name - "
                   "say which, as '" + m.declaredIn->localName() + "::" + m.name + "'");
}

// `c.A::x` and `p->A::get()` (A18): the name read as the member is a class -
// the object's own or a base of it - and the member follows the `::`. Answers
// that class with `name` moved on to the member, or null with nothing read.
const Type *Parser::qualifiedMemberScope(const Type *obj, std::string &name,
                                         std::size_t pos) {
    if (!peek().is("::") || peekAt(1).kind != TokenKind::Ident) return nullptr;
    std::string q = name;
    for (;;) {
        at_++;
        name = peek().text;
        at_++;
        if (!peek().is("::") || peekAt(1).kind != TokenKind::Ident) break;
        q += "::" + name;
    }
    // [basic.lookup.classref]/4: the class of the object is looked in first - `d.base::f()` for its typedef.
    const std::function<const Type *(const Type *)> memberType = [&](const Type *c) -> const Type * {
        if (const Type *t = findTypedef(c->unqualified()->tag() + "::" + q)) return t;
        for (const Type::BaseSpec &b : c->unqualified()->bases())
            if (const Type *t = memberType(b.type)) return t;
        return nullptr;
    };
    const Type *cls = memberType(obj);
    if (cls == nullptr) cls = findTypedef(q);
    if (cls == nullptr) cls = FindBase::named(obj, q);
    if (cls == nullptr || !cls->isStructOrUnion())
        src_.fail(pos, "'" + q + "' is not a class, so '" + q + "::" + name +
                       "' names no member of '" + obj->describe() + "'");
    cls = cls->unqualified();
    if (cls != obj->unqualified() && accessibleBaseOffset(obj, cls) < 0)
        src_.fail(pos, "'" + q + "' is not '" + obj->describe() + "' or a base "
                       "of it, so '" + q + "::" + name + "' is not one of its "
                       "members");
    return cls;
}

// The offset of `base` in `derived` where every step to it is a direct non-virtual base, or -1.
static int fixedBaseOffset(const Type *derived, const Type *base) {
    const Type *d = derived->unqualified();
    if (d == base->unqualified()) return 0;
    for (const Type::BaseSpec &s : d->bases()) {
        if (!s.direct || s.isVirtual) continue;
        const int deeper = fixedBaseOffset(s.type, base);
        if (deeper >= 0) return s.offset + deeper;
    }
    return -1;
}

// **`d.L::v`: L's member in d's L subobject** ([class.qual], [expr.ref]/4),
// `m` L's record: a constant offset leaves the object and sets *offset; a
// virtual step converts the object to L by derived-to-base, which knows all.
ExprPtr Parser::qualifiedSubobject(ExprPtr object, const Type *obj, const Type *base,
                                   const Member &m, int *offset) {
    *offset = 0;
    if (base == nullptr || base->unqualified() == obj->unqualified()) return object;
    const int fixed = fixedBaseOffset(obj, base);
    if (fixed >= 0 && m.inVirtualBase == nullptr) {
        *offset = fixed;
        return object;
    }
    const Type *to = obj->isConst() ? types_.withConst(base->unqualified()) : base->unqualified();
    ExprPtr at(new Unary('&', std::move(object)));
    at->setType(types_.pointerTo(obj));
    ExprPtr there(new Unary('*', convert(std::move(at), types_.pointerTo(to))));
    there->setType(to);
    return there;
}

// [class.dtor]/14 and [expr.pseudo]: the object is the pointee for `->` and
// the operand itself for `.`; the name after `~` must be the object's type,
// its class, or a base of it - and for a scalar the whole thing is a no-op.
ExprPtr Parser::explicitDestructorCall(ExprPtr &object, std::size_t pos) {
    const bool arrow = peek().is("->");
    // One name of the path: an identifier, and its template arguments where a `<` after it closes just before
    // `next` - `One<int>::~One`, `~One<int>()`, `~Held<>()` ([class.dtor]/14 names the class by any type-name,
    // and a template-id is one; until 1.74 the path was identifiers only and these were "expected '('").
    // Answers where the name ends, 0 where none stands at k.
    auto nameEnd = [&](std::size_t k, const char *next) -> std::size_t {
        if (peekAt(k).kind != TokenKind::Ident) return 0;
        if (!peekAt(k + 1).is("<")) return k + 1;
        int depth = 0;
        for (std::size_t j = k + 1;; j++) {
            const Token &t = peekAt(j);
            if (t.kind == TokenKind::End || t.is(";") || t.is("{") || t.is("}")) return 0;
            if (depth > 0 && (t.is("(") || t.is("["))) { j = groupCloseAt(at_ + j) - at_; continue; }
            if (t.is("<")) depth++;
            else if (t.is(">")) depth--;
            else if (t.is(">>")) depth -= 2;
            if (depth <= 0) return depth == 0 && peekAt(j + 1).is(next) ? j + 1 : 0;
        }
    };
    std::size_t k = 1;
    bool pathHasArguments = false;
    for (;;) {
        const std::size_t e = nameEnd(k, "::");
        if (e == 0 || !peekAt(e).is("::")) break;
        pathHasArguments = pathHasArguments || e != k + 1;
        k = e + 1;
    }
    if (!peekAt(k).is("~") || peekAt(k + 1).kind != TokenKind::Ident) return nullptr;
    const bool nameHasArguments = peekAt(k + 2).is("<");
    if (nameHasArguments && nameEnd(k + 1, "(") == 0) return nullptr;
    if (arrow && object->type()->unqualified()->isStructOrUnion()) return nullptr;
    const std::size_t tildeAt = at_ + k;
    at_++;
    std::string qualifier;
    const Type *pathType = nullptr;          // the path read as a type, when a name of it has arguments
    if (pathHasArguments) {
        const std::size_t pathPos = peek().pos;
        // the `::` before `~` is the path's end, not a member of the type before it: read up to it
        const Token colons = tokens_[tildeAt - 1];
        tokens_[tildeAt - 1].kind = TokenKind::End;
        StorageClass psc;
        Qualifiers pquals;
        pathType = specifiers(&psc, &pquals);
        const bool whole = at_ == tildeAt - 1;
        tokens_[tildeAt - 1] = colons;
        if (pathType == nullptr || !whole)
            src_.fail(pathPos, "the name before '::~' names no type, so no destructor either");
        pathType = pathType->unqualified();
        qualifier = pathType->describe();
        at_ = tildeAt;
    } else {
        while (peek().kind == TokenKind::Ident && peekAt(1).is("::")) {
            if (!qualifier.empty()) qualifier += "::";
            qualifier += peek().text;
            at_ += 2;
        }
    }
    at_++;
    const std::string name = peek().text;
    // a class template's own name stands for the specialization ([temp.local]/1): `p->~Held()`, `One<int>::~One`
    auto templateNamed = [&](const Type *cls) {
        const Type *u = cls->unqualified();
        return u->isStructOrUnion() && !u->templateName().empty() &&
               (u->templateName() == name || localOf(u->templateName()) == name);
    };
    const Type *nameType = nullptr;          // `~Name<arguments>`, read as the type it is
    if (nameHasArguments) {
        const std::size_t namePos = peek().pos;
        // [basic.lookup.classref]/3: the name is also looked up in the class of the object, where the template's
        // own name is - `a->~Arr<char, 3>()` for an `ns::Arr` that nothing brings into this scope.
        const Type *in = pathType;
        if (in == nullptr) {
            const Type *ot = object->type();
            if (arrow) ot = ot->isPointer() ? ot->pointee() : nullptr;
            if (ot != nullptr && ot->unqualified()->isStructOrUnion()) in = ot->unqualified();
        }
        std::map<std::string, TemplateDecl>::const_iterator own = templates_.end();
        if (in != nullptr && templateNamed(in)) own = templates_.find(templateKeyOf(in));
        if (own != templates_.end() && own->second.isClass) {
            const TemplateDecl decl = own->second;
            at_++;
            nameType = instantiateClass(decl, namePos);
        } else {
            StorageClass nsc;
            Qualifiers nquals;
            nameType = specifiers(&nsc, &nquals);
        }
        if (nameType == nullptr)
            src_.fail(namePos, "'~" + name + "<...>' names no type this compiler knows, so it is not a destructor");
        nameType = nameType->unqualified();
    } else {
        at_++;
    }
    expect("(");
    expect(")");

    ExprPtr n = std::move(object);
    if (arrow) n = decay(std::move(n));
    if (arrow && !n->type()->isPointer())
        src_.fail(pos, "'->' needs a pointer, not '" + n->type()->describe() + "'");
    const Type *obj = arrow ? n->type()->pointee() : n->type();
    const Type *plain = obj->unqualified();

    // **Which type the name means.** `~T` alone names T; `B::~B` names B,
    // which must be the object's class or a base; a typedef reaches either.
    const Type *named = nullptr;
    if (!qualifier.empty()) {
        named = pathType != nullptr ? pathType : findTypedef(qualifier);
        if (named == nullptr && plain->isStructOrUnion())
            named = FindBase::named(plain, qualifier);
        if (named == nullptr)
            src_.fail(pos, "'" + qualifier + "' names no type, so '" + qualifier +
                           "::~" + name + "' names no destructor");
        const Type *inner = nameType != nullptr ? nameType : findTypedef(name);
        // [basic.lookup.qual]/6: the name after `::~` is looked up where the path's last name was -
        // `s->std::string::~string()`, with no `string` in this scope.
        if (inner == nullptr && pathType == nullptr && qualifier.rfind("::") != std::string::npos)
            inner = findTypedef(qualifier.substr(0, qualifier.rfind("::") + 2) + name);
        if ((nameType != nullptr || (name != localOf(named->unqualified()->tag()) && !templateNamed(named))) &&
            (inner == nullptr || inner->unqualified() != named->unqualified()))
            src_.fail(pos, "'~" + name + "' is not the destructor of '" +
                           named->describe() + "'");
    } else {
        named = nameType != nullptr ? nameType : findTypedef(name);
        if (named == nullptr && plain->isStructOrUnion() &&
            (plain->localName() == name || templateNamed(plain)))
            named = plain;
        if (named == nullptr)
            src_.fail(pos, "'~" + name + "' names no type this compiler knows, "
                           "so it is not a destructor");
    }
    named = named->unqualified();

    // **[expr.pseudo]: a scalar has nothing to destroy**, and the only effect
    // is that the operand is evaluated - the pointer, or the object's address.
    if (!plain->isStructOrUnion()) {
        if (named != plain)
            src_.fail(pos, "the pseudo-destructor '~" + name + "' does not name "
                           "the type of this object, which is '" +
                           plain->describe() + "'");
        ExprPtr done;
        if (!arrow && clonePure(*n) != nullptr) {
            done.reset(new Num(0LL));
            done->setType(types_.intType());
        } else if (!arrow && isGlvalue(*n)) {
            done.reset(new Unary('&', std::move(n)));
            done->setType(types_.pointerTo(obj));
        } else {
            done = std::move(n);
        }
        ExprPtr c(new Cast(types_.get(Kind::Void), std::move(done)));
        c->setType(types_.get(Kind::Void));
        return c;
    }

    if (named != plain && publicBaseOffset(plain, named) < 0)
        src_.fail(pos, "'" + named->describe() + "' is not '" + plain->describe() +
                       "' or a base of it, so '~" + name + "' cannot destroy this");
    const Type *cls = named;
    const Signature *dtor = destructorOf(cls);
    ExprPtr addr;
    if (arrow) {
        addr = std::move(n);
    } else if (clonePure(*n) != nullptr || isGlvalue(*n)) {
        addr.reset(new Unary('&', std::move(n)));
        addr->setType(types_.pointerTo(obj));
    } else {
        src_.fail(pos, "'.~" + name + "()' needs an object with an address");
    }
    // A class with nothing to run is the scalar case in a class's clothes.
    if (dtor == nullptr) {
        ExprPtr c(new Cast(types_.get(Kind::Void), std::move(addr)));
        c->setType(types_.get(Kind::Void));
        return c;
    }
    if (dtor->access != Access::Public && !insideAccessOf(cls, dtor->access) &&
        !isFriendOf(cls)) {
        const char *how = dtor->access == Access::Private ? "private" : "protected";
        src_.fail(pos, "the destructor of '" + cls->describe() + "' is " + how +
                       ", and this is outside the class");
    }
    const Type *thisType = types_.pointerTo(obj->isConst() ? types_.withConst(cls) : cls);
    if (cls != plain) addr = convert(std::move(addr), thisType);
    else addr->setType(thisType);

    // **A qualified call is never dispatched** - [expr.call]/1 - and an
    // unqualified one on a virtual destructor reaches the most derived one.
    if (!dtor->isVirtual || !qualifier.empty())
        return destructorCall(std::move(addr), *dtor, pos);
    const int slot = allocateFrameSlot(thisType);
    const std::string temp = ".dx" + std::to_string(refTemps_++);
    ExprPtr keep(Var::local(temp, slot));
    keep->setType(thisType);
    ExprPtr save(new Assign(std::move(keep), std::move(addr)));
    save->setType(thisType);
    ExprPtr call = virtualDestructorCall(temp, slot, thisType, cls, false, pos);
    ExprPtr both(new Comma(std::move(save), std::move(call)));
    both->setType(types_.get(Kind::Void));
    return both;
}

ExprPtr Parser::postfix() {
    ExprPtr n = primary(current_);
    for (;;) {
        std::size_t pos = peek().pos;

        // `f(1, 2)` where f is an object. **[over.call]: the call operator has to be
        // a non-static member function** - there is no non-member form of it - so the
        // candidate set is the class's own and memberCall's resolution is it.
        if (peek().is("(") && n->type()->unqualified()->isStructOrUnion()) {
            const Type *self = n->type();
            // **[over.call.object]/2: the surrogate call** - a class with no `operator()` of its own that
            // converts to a pointer or a reference to a function is called through what it converts to
            // (R5). The conversion is called here; the `(` is read below as a call through the pointer.
            if (!self->isMemberFunctionPointer() && findMemberOwner(self->unqualified(), "operator()") == nullptr) {
                const Signature *surrogate = nullptr;
                bool several = false;
                for (const Type *c = self->unqualified(); c != nullptr; c = c->base())
                    for (std::size_t k = 0; k < functions_.size(); k++) {
                        const Signature &f = functions_[k];
                        if (f.owner != c->tag() || !isConversionName(f.name) || !f.params.empty() || f.isExplicit)
                            continue;
                        const Type *gives = f.returns->isReference() ? f.returns->referent() : f.returns;
                        if (!gives->isFunctionPointer() && !gives->isFunction()) continue;
                        if (surrogate != nullptr) several = true;
                        surrogate = &f;
                    }
                if (surrogate != nullptr && !several) {
                    const std::string conv = surrogate->name;
                    n = memberCallWith(std::move(n), self, conv, pos, std::vector<ExprPtr>());
                    continue;
                }
            }
            at_++;
            n = memberCall(std::move(n), self, "operator()", pos);
            continue;
        }

        // **A function lvalue - what a reference to a function reads as - is called through
        // its address**, [conv.func]: `&*slot`, which every backend takes as the slot's value.
        if (peek().is("(") && n->type()->isFunction()) {
            const Type *fp = types_.pointerTo(n->type());
            n.reset(new Unary('&', std::move(n)));
            n->setType(fp);
        }
        if (peek().is("(") && n->type()->isFunctionPointer()) {
            at_++;
            const Type *fn = n->type()->pointee();
            std::string called = n->type()->describe();
            // **A member function pointer's object, picked up here.** `o.*p` left the
            // address behind because no expression holds a pair; this is the one
            // place it is wanted, in front of the arguments as the callee's `this`.
            if (boundThis_ != nullptr && boundFn_ == fn) {
                ExprPtr self = std::move(boundThis_);
                boundFn_ = nullptr;
                std::vector<ExprPtr> args;
                args.push_back(std::move(self));
                std::vector<ExprPtr> written;
                parseArguments(written);
                for (std::size_t i = 0; i < written.size(); i++)
                    args.push_back(std::move(written[i]));
                std::vector<const Type *> full;
                const Type *thisAs = types_.get(Kind::Void);
                if (fn->cvThis() & Type::kThisConst) thisAs = types_.withConst(thisAs);
                if (fn->cvThis() & Type::kThisVolatile) thisAs = types_.withVolatile(thisAs);
                full.push_back(types_.pointerTo(thisAs));
                for (std::size_t i = 0; i < fn->params().size(); i++)
                    full.push_back(fn->params()[i]);
                // A call through a member function pointer is a member call, and the
                // Microsoft ABI's answers - `this` first, a hidden return pointer for
                // a class of any size - follow the call and not the spelling.
                n = completeCall(called, std::string(), std::move(n),
                                 fn->returns(), full, fn->isVariadicFn(), pos,
                                 std::move(args), true);
                continue;
            }
            n = finishCall(called, called, std::move(n), fn->returns(),
                           fn->params(), fn->isVariadicFn(), pos);
            continue;
        }

        if (peek().is("[")) {
            at_++;
            ExprPtr index = expr();
            expect("]");
            // **A class is subscripted by its own operator, not by pointer
            // arithmetic.**
            const Type *subscripted = n->type()->unqualified();
            // [over.built]/14: `T &operator[](std::ptrdiff_t, T *)` - a class with one conversion to an
            // integer in front of a pointer or an array is the subscript written the other way round,
            // `a[arr]` for `arr[a]` (1.27, 2026-10-21, review of 1.25, item 3).
            if (subscripted->isStructOrUnion() &&
                (index->type()->unqualified()->isPointer() || index->type()->unqualified()->isArray()) &&
                findMemberOwner(subscripted, "operator[]") == nullptr) {
                const Signature *asInteger = soleNumericConversion(subscripted);
                if (asInteger != nullptr && asInteger->returns->unqualified()->isInteger()) {
                    n.swap(index);
                    subscripted = n->type()->unqualified();
                }
            }
            // [over.built]/13: `T &operator[](T *, std::ptrdiff_t)` - a class with no operator[] and one
            // conversion to a pointer is subscripted as the pointer it converts to.
            if (subscripted->isStructOrUnion() && findMemberOwner(subscripted, "operator[]") == nullptr) {
                const Signature *asPointer = soleNumericConversion(n->type());
                if (asPointer != nullptr && asPointer->returns->unqualified()->isPointer()) {
                    n = builtinOperand(std::move(n), pos);
                    subscripted = n->type()->unqualified();
                }
            }
            if (subscripted->isStructOrUnion()) {
                if (findMemberOwner(subscripted, "operator[]") == nullptr)
                    src_.fail(pos, "'" + subscripted->describe() + "' is "
                                   "subscripted here and declares no "
                                   "'operator[]' - a class is not an array, so "
                                   "there is no built-in meaning to fall back "
                                   "on");
                std::vector<ExprPtr> args;
                args.push_back(std::move(index));
                // The type is read out first: `n` is moved into the call, and
                // the order the two arguments are evaluated in is not fixed.
                const Type *objectType = n->type();
                n = memberCallWith(std::move(n), objectType, "operator[]", pos,
                                   std::move(args));
                continue;
            }
            ExprPtr sum = arithmetic(BinOp::Add, std::move(n), std::move(index), pos);
            if (!sum->type()->isPointer())
                src_.fail(pos, "subscript needs an array or a pointer");
            const Type *elem = sum->type()->pointee();
            ExprPtr deref(new Unary('*', std::move(sum)));
            deref->setType(elem);
            n = std::move(deref);
            continue;
        }

        // `p->~T()` and `o.~T()` are read before either member path, which
        // want a name where the `~` is.
        if (peek().is("->") || peek().is(".")) {
            if (ExprPtr done = explicitDestructorCall(n, pos)) {
                n = std::move(done);
                continue;
            }
        }

        if (peek().is("->")) {
            at_++;
            // **[over.ref]: a class on the left of `->` is asked for a
            // pointer, and the answer is asked again.**
            for (int hops = 0; n->type()->unqualified()->isStructOrUnion(); hops++) {
                const Type *held = n->type()->unqualified();
                if (findMemberOwner(held, "operator->") == nullptr)
                    src_.fail(pos, "'->' needs a pointer, and '" +
                                   held->describe() + "' is a class that "
                                   "declares no 'operator->' to get one from");
                if (hops == 16)
                    src_.fail(pos, "'operator->' on '" + held->describe() +
                                   "' keeps answering with a class that has "
                                   "one too - [over.ref] applies it again each "
                                   "time, so this never reaches a pointer");
                std::vector<ExprPtr> args;
                const Type *objectType = n->type();
                n = memberCallWith(std::move(n), objectType, "operator->", pos,
                                   std::move(args));
            }
            if (!n->type()->isPointer() || !n->type()->pointee()->isStructOrUnion())
                src_.fail(pos, "'->' needs a pointer to a struct or union, not '" +
                               n->type()->describe() + "'");
            // A member class of a specialization is made when it is first needed ([temp.inst]/1), and naming a
            // member through a pointer to it is that need: `p_->__call(...)` in function<R(Args...)>.
            const Type *obj = realizeMemberClass(n->type()->pointee(), true);
            ExprPtr deref(new Unary('*', std::move(n)));
            deref->setType(obj);
            n = std::move(deref);
            // [temp.names]/4: `p->template f<3>()` - the keyword only says a `<` opens arguments, which
            // token replay already knows; it is read and dropped, as `typename` is.
            consume("template");
            std::string name = declaredName("a member name");
            const Type *scope = qualifiedMemberScope(obj, name, pos);
            const Type *in = scope != nullptr ? scope : obj;
            // **A member function template**, `p->head<3>()` - told from `<` as
            // a comparison only because the member is a registered template.
            if ((peek().is("<") || (peek().is("(") && !hasOrdinaryMember(obj, name))) &&
                memberTemplateOwner(obj, name) != nullptr) {
                n = memberTemplateCall(std::move(n), obj, name, pos);
                continue;
            }
            // **A data member that is itself callable**: `p.H(i, j)` reaches H
            // and then applies its operator(), where `p.f(i)` calls a member
            // function f.
            if (findMemberOwner(in->unqualified(), name) != nullptr &&
                consume("(")) {
                // A qualified call is never dispatched - [expr.call]/1.
                std::vector<ExprPtr> args;
                parseArguments(args);
                n = memberCallWith(std::move(n), obj, name, pos, std::move(args),
                                   scope);
                continue;
            }
            // **`p->count` where count is static** names the one shared
            // object, and the expression on the left is still evaluated -
            // [expr.ref] says so - which is what the comma is for.
            if (const Type::StaticMember *s = in->findStaticMember(name)) {
                ExprPtr one = staticMemberRef(in, *s, in->tag(), pos);
                // [expr.ref] evaluates the object expression even though what the
                // whole thing names is the one shared object. Where it is pure
                // there is nothing to evaluate, and dropping it leaves an lvalue.
                if (clonePure(*n) == nullptr) {
                    const Type *st = one->type();
                    ExprPtr both(new Comma(std::move(n), std::move(one)));
                    both->setType(st);
                    one = std::move(both);
                }
                n = std::move(one);
                continue;
            }
            const Member *m = in->findMember(name);
            if (!m) src_.fail(pos, "'" + in->describe() + "' has no member '" + name + "'");
            if (m->ambiguous) refuseAmbiguousMember(in, *m, pos);
            checkAccessible(obj, *m, pos);
            int adjust = 0;
            n = qualifiedSubobject(std::move(n), obj, scope, *m, &adjust);
            // A member of a virtual base is not at a constant offset. The test
            // comes first because the call consumes the object: asking
            // unconditionally moved `n` away from every ordinary access too.
            if (m->inVirtualBase != nullptr) {
                const Type *from = n->type();
                refuseVirtualBaseMember(from, *m, name, pos);
                if (ExprPtr viaVb = virtualBaseMember(std::move(n), from, *m)) {
                    n = std::move(viaVb);
                    continue;
                }
            }
            ExprPtr acc(new MemberAccess(std::move(n), name, m->offset + adjust,
                                         m->width, m->bitOffset));
            // A member reached through a const object is itself const - [expr.ref]
            // gives it the object's qualification. **But not a reference member's
            // referent**: [dcl.ref] stops the const at the reference itself.
            acc->setType(memberTypeIn(obj, *m));
            // A reference member holds an address, so reading one is a
            // dereference - the same `useReference` every mention of a
            // reference local already goes through.
            acc = useReference(std::move(acc));
            n = std::move(acc);
            continue;
        }

        if (peek().is("++") || peek().is("--")) {
            bool up = peek().is("++");
            at_++;
            n = incDec(std::move(n), up, false, pos);
            continue;
        }

        if (peek().is(".")) {
            at_++;
            if (!n->type()->isStructOrUnion())
                src_.fail(pos, "'.' needs a struct or union, not '" +
                               n->type()->describe() + "'");
            const Type *obj = realizeMemberClass(n->type(), true);
            // [temp.names]/4: `p->template f<3>()` - the keyword only says a `<` opens arguments, which
            // token replay already knows; it is read and dropped, as `typename` is.
            consume("template");
            std::string name = declaredName("a member name");
            const Type *scope = qualifiedMemberScope(obj, name, pos);
            const Type *in = scope != nullptr ? scope : obj;
            // **A member function template**, `v.head<3>()`.
            if ((peek().is("<") || (peek().is("(") && !hasOrdinaryMember(obj, name))) &&
                memberTemplateOwner(obj, name) != nullptr) {
                n = memberTemplateCall(std::move(n), obj, name, pos);
                continue;
            }
            // **A data member that is itself callable**: `p.H(i, j)` reaches H
            // and then applies its operator(), where `p.f(i)` calls a member
            // function f.
            if (findMemberOwner(in->unqualified(), name) != nullptr &&
                consume("(")) {
                // A qualified call is never dispatched - [expr.call]/1.
                std::vector<ExprPtr> args;
                parseArguments(args);
                n = memberCallWith(std::move(n), obj, name, pos, std::move(args),
                                   scope);
                continue;
            }
            // **`p->count` where count is static** names the one shared
            // object, and the expression on the left is still evaluated -
            // [expr.ref] says so - which is what the comma is for.
            if (const Type::StaticMember *s = in->findStaticMember(name)) {
                ExprPtr one = staticMemberRef(in, *s, in->tag(), pos);
                // [expr.ref] evaluates the object expression even though what the
                // whole thing names is the one shared object. Where it is pure
                // there is nothing to evaluate, and dropping it leaves an lvalue.
                if (clonePure(*n) == nullptr) {
                    const Type *st = one->type();
                    ExprPtr both(new Comma(std::move(n), std::move(one)));
                    both->setType(st);
                    one = std::move(both);
                }
                n = std::move(one);
                continue;
            }
            const Member *m = in->findMember(name);
            if (!m) src_.fail(pos, "'" + in->describe() + "' has no member '" + name + "'");
            if (m->ambiguous) refuseAmbiguousMember(in, *m, pos);
            checkAccessible(obj, *m, pos);
            // [expr.ref]/4: a non-reference member of an rvalue object is an xvalue - `std::move(s).s`
            // and `make().s` bind `std::string &&` (1.27, 2026-10-21, review of 1.25, item 3: they
            // were lvalues, and `take(const std::string &)` took them).
            const bool objectIsRvalue = n->isXvalue() || !isLvalue(*n);
            int adjust = 0;
            n = qualifiedSubobject(std::move(n), obj, scope, *m, &adjust);
            // A member of a virtual base is not at a constant offset. The test
            // comes first because the call consumes the object: asking
            // unconditionally moved `n` away from every ordinary access too.
            if (m->inVirtualBase != nullptr) {
                const Type *from = n->type();
                refuseVirtualBaseMember(from, *m, name, pos);
                if (ExprPtr viaVb = virtualBaseMember(std::move(n), from, *m)) {
                    n = std::move(viaVb);
                    continue;
                }
            }
            ExprPtr acc(new MemberAccess(std::move(n), name, m->offset + adjust,
                                         m->width, m->bitOffset));
            // A member reached through a const object is itself const - [expr.ref]
            // gives it the object's qualification. **But not a reference member's
            // referent**: [dcl.ref] stops the const at the reference itself.
            acc->setType(memberTypeIn(obj, *m));
            if (objectIsRvalue && !m->type->isReference()) acc->setXvalue();
            // A reference member holds an address, so reading one is a
            // dereference - the same `useReference` every mention of a
            // reference local already goes through.
            acc = useReference(std::move(acc));
            n = std::move(acc);
            continue;
        }

        return n;
    }
}

ExprPtr Parser::unary() {
    std::size_t pos = peek().pos;

    // Unary `+` is a no-op on a built-in operand and is not one on a class, where
    // [over.match.oper] makes it a call to `operator+` with no argument. There is no
    // path to that yet, so the *use* is refused rather than passed through.
    if (consume("+")) {
        ExprPtr v = castExpr();
        if (ExprPtr call = overloadedUnary("+", v, pos)) return call;
        v = decay(std::move(v));
        v = contextualScalar(std::move(v), pos, "unary '+'");
        // [expr.unary.op]/7: the integral promotion applies, so `+c` is an int.
        if (v->type()->isArithmetic()) {
            const Type *t = promote(v->type());
            if (t != v->type()) v = convert(std::move(v), t);
        }
        v->setDeclCategory(Expr::Category::Prvalue);   // `+t` is a value, not t
        return v;
    }

    if (peek().is("++") || peek().is("--")) {
        bool inc = peek().is("++");
        at_++;
        ExprPtr operand = unary();
        const bool scalar = !operand->type()->unqualified()->isStructOrUnion();
        ExprPtr r = incDec(std::move(operand), inc, true, pos);
        if (scalar) r->setDeclCategory(Expr::Category::Lvalue);   // [expr.pre.incr]/1
        return r;
    }
    if (consume("~")) {
        ExprPtr v = castExpr();
        if (ExprPtr call = overloadedUnary("~", v, pos)) return call;
        v = decay(builtinOperand(std::move(v), pos));
        refuseScopedOperand(v->type(), nullptr, "~", pos);
        if (!v->type()->isInteger())
            src_.fail(pos, "'~' needs an integer, not '" + v->type()->describe() + "'");
        const Type *t = promote(v->type());
        ExprPtr ones(new Num(-1LL));
        ones->setType(t);
        ExprPtr n(new Binary(BinOp::BitXor, convert(std::move(v), t), std::move(ones)));
        n->setType(t);
        return n;
    }
    if (consume("!")) {
        ExprPtr v = castExpr();
        if (ExprPtr call = overloadedUnary("!", v, pos)) return call;
        v = decay(std::move(v));
        v = contextualScalar(std::move(v), pos, "'!'");
        ExprPtr node(new Unary('!', std::move(v)));
        node->setType(types_.get(Kind::Bool));   // [expr.unary.op]/9
        return node;
    }
    if (consume("-")) {
        ExprPtr v = castExpr();
        if (ExprPtr call = overloadedUnary("-", v, pos)) return call;
        v = decay(builtinOperand(std::move(v), pos));
        refuseScopedOperand(v->type(), nullptr, "-", pos);
        if (!v->type()->isArithmetic())
            src_.fail(pos, "unary '-' needs a number, not '" + v->type()->describe() + "'");
        const Type *t = promote(v->type());
        ExprPtr n(new Unary('-', convert(std::move(v), t)));
        n->setType(t);
        return n;
    }
    if (consume("&")) {
        // **`&S::x` - a pointer to a member, which is an offset and not an address.**
        // Read here because nothing else would: `primary`'s qualified-name path answers
        // for a *static* member. Every `Name::` but the last is the class (A20, nested).
        std::size_t q = 0;
        std::string qualified;
        while (peekAt(q).kind == TokenKind::Ident && peekAt(q + 1).is("::") &&
               peekAt(q + 2).kind == TokenKind::Ident) {
            qualified += (q == 0 ? "" : "::") + peekAt(q).text;
            q += 2;
        }
        // `&std::pair<int, int>::first` stops the walk at `pair<`: its qualifier is a template-id.
        std::size_t named = peekAt(q + 1).is("<") ? 0 : q;
        const Type *qualifiedClass = named > 0 ? findTypedef(qualified) : templateIdQualifier(&named);
        if (named > 0) {
            const std::string memberName = peekAt(named).text;
            if (const Type *cls = qualifiedClass)
                if (cls->isStructOrUnion()) {
                    if (const Member *m = cls->findMember(memberName)) {
                        checkAccessible(cls, *m, pos);
                        at_ += named + 1;
                        ExprPtr off(new Num(static_cast<long long>(m->offset)));
                        off->setType(types_.memberPointerTo(cls, m->type));
                        return off;
                    }
                    // `&S::f` for a member function: the ABI's pair, built in
                    // a slot of this frame the way a class temporary is.
                    const std::string key = cls->tag() + "::" + memberName;
                    if (const std::vector<std::size_t> *set = overloadsOf(key)) {
                        const Signature *picked = set->size() > 1 ? pickByTarget(*set, cls, overloadTarget_)
                                                                  : nullptr;
                        if (set->size() > 1 && picked == nullptr && overloadArgument_) {
                            at_ += named + 1;
                            return overloadPlaceholder(*set, key, cls, pos);
                        }
                        if (set->size() > 1 && picked == nullptr)
                            src_.fail(pos, "'" + key + "' names " +
                                           std::to_string(set->size()) +
                                           " functions, and which one this is "
                                           "cannot be told from the use alone");
                        const Signature &f = picked != nullptr ? *picked : functions_[(*set)[0]];
                        at_ += named + 1;
                        // [expr.unary.op]/3: `&S::f` is a prvalue, though its pair is built in a slot - a
                        // forwarding reference deduces the pointer's type itself from it, not a reference.
                        ExprPtr made = memberFunctionAddress(cls, f, key, pos);
                        made->setDeclCategory(Expr::Category::Prvalue);
                        return made;
                    }
                    // `&Outer::K` for a static member: the object itself, whose value a plain mention
                    // folds to a constant - [class.static.data]/3 wants its definition for this (1.27,
                    // 2026-10-21, review of 1.25, item 3: the folded constant had no address).
                    if (const Type::StaticMember *s = cls->findStaticMember(memberName))
                        if (!s->isThreadLocal) {
                            if (s->access != Access::Public && !insideAccessOf(cls, s->access) &&
                                !isFriendOf(cls))
                                src_.fail(pos, "'" + cls->tag() + "::" + s->name + "' is " +
                                               (s->access == Access::Private ? "private" : "protected"));
                            at_ += named + 1;
                            if (unevaluated_ == 0) usedStaticMembers_.insert(s->symbol);
                            // With none written outside the class, the one cl lays down where it is used:
                            // weak, the value the class gave it, and a later definition adding nothing.
                            if (s->folded && unevaluated_ == 0 && !inClassConstexprStatics_.count(s->symbol)) {
                                bool defined = false;
                                for (const Global &g : current_->globals) defined = defined || g.symbol == s->symbol;
                                if (!defined) {
                                    GlobalPiece p;
                                    p.offset = 0;
                                    p.size = s->type->size(target_);
                                    p.value = s->value;
                                    current_->globals.push_back(Global{ cls->tag() + "::" + s->name, s->symbol, s->type,
                                                                        std::vector<GlobalPiece>(1, p), true, false,
                                                                        true });
                                    current_->globals.back().isInline = true;
                                    inClassConstexprStatics_.insert(s->symbol);
                                }
                            }
                            Var *sv = Var::global(cls->tag() + "::" + s->name);
                            sv->setSymbol(s->symbol);
                            ExprPtr object(sv);
                            object->setType(s->type);
                            object = useReference(std::move(object));
                            const Type *of = object->type();
                            ExprPtr addr(new Unary('&', std::move(object)));
                            addr->setType(types_.pointerTo(of));
                            return addr;
                        }
                }
        }
        ExprPtr v = castExpr();
        // `&f` for an overloaded f as an argument is the set itself, still waiting for its parameter.
        if (overloadArgOf(*v) != nullptr) return v;
        // `&*pf`: the address of the function `*pf` is, which is pf's value (1.80: it was `&pf`).
        if (v->isFunctionDesignator()) { v->setFunctionDesignator(false); return v; }
        // Only when the class declared one.
        if (ExprPtr call = overloadedUnary("&", v, pos)) return call;
        // `&(a = b)`, `&++a`: the address of the operand the assignment names ([expr.ass]/1), once it is done.
        if (ExprPtr first = takeAssigned(v)) {
            const Type *obj = v->type();
            ExprPtr at(new Unary('&', std::move(v)));
            at->setType(types_.pointerTo(obj));
            return afterAssigned(std::move(first), std::move(at));
        }
        // **`&f` and `f` are the same thing for a function.** [conv.func] has already
        // turned the designator into a pointer, so taking its address again built a
        // pointer to a pointer. `&p` for a variable is an ordinary address-of.
        if (const Unary *decayed = dynamic_cast<const Unary *>(v.get()))
            if (decayed->op() == '&' && v->type()->isPointer() &&
                v->type()->pointee()->isFunction())
                return v;
        if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(v.get()))
            if (m->isBitField())
                src_.fail(pos, "'" + m->name() + "' is a bit-field, and a "
                               "bit-field has no address");
        if (const Var *rv = dynamic_cast<const Var *>(v.get()))
            if (rv->noAddress())
                src_.fail(pos, "'" + rv->name() + "' is register, and a register "
                               "object has no address - drop the register");
        if (dynamic_cast<const Conditional *>(v.get()) != nullptr)
            src_.fail(pos, "'?:' is not an lvalue, and its address cannot be "
                           "taken - assign it to something first");
        if (dynamic_cast<const Call *>(v.get()) != nullptr)
            src_.fail(pos, "a call is not an lvalue, and its address cannot be "
                           "taken - assign it to something first");
        const Type *of = v->type();
        ExprPtr n(new Unary('&', std::move(v)));
        n->setType(types_.pointerTo(of));
        return n;
    }
    if (consume("*")) {
        ExprPtr v = castExpr();
        if (ExprPtr call = overloadedUnary("*", v, pos)) return call;
        v = decay(builtinOperand(std::move(v), pos));
        if (!v->type()->isPointer())
            src_.fail(pos, "'*' needs a pointer, not '" + v->type()->describe() + "'");

        if (v->type()->pointee()->isFunction()) { v->setFunctionDesignator(true); return v; }
        const Type *elem = v->type()->pointee();
        if (elem->isVoid()) src_.fail(pos, "'void *' cannot be dereferenced");
        ExprPtr n(new Unary('*', std::move(v)));
        n->setType(elem);
        return n;
    }
    // `::new` and `::delete` - [expr.new]/9, [expr.delete]/9: the global allocation functions, a class's
    // own passed over (R27's `::new (buf) S(2)`).
    if (peek().is("::") && (peekAt(1).is("new") || peekAt(1).is("delete"))) {
        std::size_t pos = peek().pos;
        const bool isNew = peekAt(1).is("new");
        at_ += 2;
        globalNewDelete_ = true;
        return isNew ? newExpression(pos) : deleteExpression(pos);
    }
    if (peek().is("new")) {
        std::size_t pos = peek().pos;
        at_++;
        return newExpression(pos);
    }
    if (peek().is("delete")) {
        std::size_t pos = peek().pos;
        at_++;
        return deleteExpression(pos);
    }

    // `sizeof...(Ts)` and `sizeof...(args)` - how many, not how big. Both
    // spellings name the same pack: the parameter and the function parameter
    // made from it are one entry here.
    if (peek().is("sizeof") && peekAt(1).is("...")) {
        const std::size_t spos = peek().pos;
        at_ += 2;
        expect("(");
        if (peek().kind != TokenKind::Ident)
            src_.fail(peek().pos, "'sizeof...' takes the name of a parameter "
                                  "pack");
        auto pack = packs_.find(peek().text);
        if (pack == packs_.end())
            src_.fail(peek().pos, "'" + peek().text + "' is not a parameter "
                                  "pack");
        const long long n = static_cast<long long>(pack->second.types.size());
        at_++;
        expect(")");
        (void)spos;
        ExprPtr n2(new Num(n));
        n2->setType(types_.get(target_.sizeType()));
        return n2;
    }

    // **`noexcept(e)` - a question about `e`, answered without running it.** The
    // operand is parsed for its meaning and thrown away as `sizeof`'s is; what is
    // kept is whether anything in it could throw, counted by `mayThrow_`.
    if (peek().is("noexcept") && peekAt(1).is("(")) {
        at_ += 2;
        const int outer = mayThrow_;
        mayThrow_ = 0;
        // Unevaluated: what it accumulates goes back. [expr.unary.noexcept]/2
        Discarded held(this);
        Unevaluated unevaluated(this);
        const std::size_t tempsBefore = pendingTemps_.size();
        (void) expr();
        bool quiet = mayThrow_ == 0;
        // [expr.unary.noexcept]/3 with [except.spec]: the operand's temporaries are destroyed in it, so a
        // destructor of theirs that may throw makes it potentially-throwing - `noexcept(Throwing())` for
        // a `~Throwing() noexcept(false)` is false, as cl has it (1.27, 2026-10-21, review of 1.25, item 3).
        for (std::size_t i = tempsBefore; quiet && i < pendingTemps_.size(); i++) {
            const Type *made = pendingTemps_[i].type;
            if (made == nullptr || !made->unqualified()->isStructOrUnion()) continue;
            if (destructorMayThrow(made->unqualified())) quiet = false;
        }
        mayThrow_ = outer;
        expect(")");
        ExprPtr n(new Num(static_cast<long long>(quiet ? 1 : 0)));
        n->setType(types_.get(Kind::Bool));
        return n;
    }

    // `alignof(T)` - [expr.alignof] takes a type-id and nothing else; the
    // answer is the type's alignment requirement, as sizeof's is its size.
    if (peek().is("alignof")) {
        at_++;
        expect("(");
        StorageClass sc;
        const Type *measured = declarator(specifiers(&sc), true).type;
        expect(")");
        if (measured->isReference()) measured = measured->referent();
        realizeMemberClass(measured, true);
        if (!measured->isComplete())
            src_.fail(pos, "alignof needs a complete type");
        ExprPtr n(new Num(static_cast<long long>(measured->align(target_))));
        n->setType(types_.get(target_.sizeType()));
        return n;
    }
    if (peek().is("sizeof")) {
        at_++;
        const Type *measured = nullptr;
        // [expr.sizeof]/1 with [dcl.ambig.res]/2: `sizeof(S::m)` is a type-id only if it can be one.
        if (peek().is("(") && parenHoldsTypeId()) {
            at_++;
            StorageClass sc;
            measured = specifiers(&sc);
            measured = declarator(measured, true).type;
            expect(")");
        } else {
            // Unevaluated: [expr.sizeof]/1. A call in here registers a result
            // slot for destruction and no object is ever built in it.
            Discarded held(this);
            Unevaluated unevaluated(this);
            NotCounted notCounted(this);
            std::size_t first = at_;
            while (tokens_[first].is("(")) first++;
            ExprPtr operand = unary();
            if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(operand.get()))
                if (m->isBitField())
                    src_.fail(pos, "sizeof cannot be applied to '" + m->name() +
                                   "', which is a bit-field");
            // [expr.sizeof]/1: a function designator is no operand - `sizeof(&f)` is the pointer's.
            if (!tokens_[first].is("&") && functionDesignated(*operand) != nullptr)
                src_.fail(pos, "sizeof cannot be applied to a function - [expr.sizeof]/1");
            measured = operand->type();
        }
        if (measured->isFunction())
            src_.fail(pos, "sizeof cannot be applied to a function type - [expr.sizeof]/1");
        // **A dependent `sizeof` outside a template argument or a `decltype`** - an array bound, a
        // `noexcept` - is the one the expression reader is not reached from.
        if (patternOnly_ && (measured->kind() == Kind::TemplateParam ||
                             measured->kind() == Kind::DependentMember))
            src_.fail(pos, "'sizeof' of a template parameter is not supported yet in this place "
                           "of a signature - in a template argument or a 'decltype' it is read");
        realizeMemberClass(measured, true);
        if (!measured->isComplete())
            src_.fail(pos, "sizeof needs a complete type");
        ExprPtr n(new Num(static_cast<long long>(measured->size(target_))));
        n->setType(types_.get(target_.sizeType()));
        return n;
    }
    return postfix();
}
