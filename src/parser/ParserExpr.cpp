// The parser: expressions, up to the operator ladder. Pointer arithmetic and the
// usual conversions, the named casts, the primary expression and the names it
// looks up, `decltype`, reference binding, and the postfix and unary levels.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>

// The pointer-width integer an offset is computed in: `long` where it is 8
// bytes, `long long` where it is not (x86_64-windows), so an index is scaled
// once at pointer width rather than in 32 bits and extended again after.
const Type *Parser::ptrdiffType() {
    const Type *l = types_.get(Kind::Long);
    return l->size(target_) == 8 ? l : types_.get(Kind::LongLong);
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

ExprPtr Parser::pointerSub(ExprPtr l, ExprPtr r, std::size_t pos) {
    if (l->type()->pointee() != r->type()->pointee())
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

// [over.match.oper]: where an operand has class type, `a @ b` is a *call* and not
// the built-in operation. Null when neither operand is a class. **A member is
// looked for on the left operand only, and the two halves are ranked together.**
ExprPtr Parser::overloadedBinary(BinOp op, ExprPtr &lhs, ExprPtr &rhs,
                                 std::size_t pos) {
    const Type *lt = lhs->type();
    const Type *rt = rhs->type();
    if (!lt->unqualified()->isStructOrUnion() &&
        !rt->unqualified()->isStructOrUnion())
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
        const Signature &sig = resolveOverload(name, args, pos);
        return completeCall(name, sig.symbol, nullptr, sig.returns, sig.params,
                            sig.variadic, pos, std::move(args),
                            !sig.owner.empty());
    }
    case OperatorChoice::None:
        break;
    }

    // **No operator took them, so the built-in one is offered a class that can
    // become a number.**
    {
        const bool leftIsClass = lt->unqualified()->isStructOrUnion();
        const bool rightIsClass = rt->unqualified()->isStructOrUnion();
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
        const Signature &sig = resolveOverload(name, args, pos);
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

    if (op == BinOp::Add) {
        if (lhs->type()->isPointer() && rhs->type()->isInteger())
            return pointerAdd(std::move(lhs), std::move(rhs));
        if (lhs->type()->isInteger() && rhs->type()->isPointer())
            return pointerAdd(std::move(rhs), std::move(lhs));
    }
    if (op == BinOp::Sub && lhs->type()->isPointer()) {
        if (rhs->type()->isInteger()) {
            const Type *lt = promote(rhs->type());
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
    if (null || lhs->type()->isPointer() || rhs->type()->isPointer() ||
        lhs->type()->isMemberPointer() || rhs->type()->isMemberPointer()) {
        // The null a data member pointer holds is -1, so nullptr becomes that first.
        if (lhs->type()->isNullPtr() && rhs->type()->isMemberPointer()) lhs = convert(std::move(lhs), rhs->type());
        if (rhs->type()->isNullPtr() && lhs->type()->isMemberPointer()) rhs = convert(std::move(rhs), lhs->type());
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
    ExprPtr v = expr();
    expect(")");

    if (to->isReference()) {
        const Type *referent = to->referent();
        v = useReference(std::move(v));
        if (!isGlvalue(*v))
            src_.fail(pos, "'static_cast<" + to->describe() + ">' needs an "
                           "object to cast, and this is a value with no "
                           "address of its own - it is already the kind of "
                           "thing a '" + to->describe() + "' binds to");
        if (v->type()->unqualified() != referent->unqualified())
            src_.fail(pos, "'static_cast<" + to->describe() + ">' of a '" +
                           v->type()->describe() + "' - casting a reference "
                           "to a different type is not supported yet, and "
                           "this one names '" + referent->describe() + "'");
        if (v->type()->isConst() && !referent->isConst())
            src_.fail(pos, "'static_cast<" + to->describe() + ">' of a '" +
                           v->type()->describe() + "' - a cast does not take "
                           "const off; 'const_cast' is what does, and the two "
                           "are written separately on purpose");
        // An lvalue reference cast leaves an lvalue and there is nothing to
        // mark; an rvalue reference cast is the whole point of this function.
        if (to->isRValueReference()) v->setXvalue();
        return v;
    }

    v = decay(std::move(v));
    if (to->isVoid()) {
        ExprPtr c(new Cast(to, std::move(v)));
        return c;
    }
    refuseUnrelatedClassCast(*v, to, pos, "'static_cast'");
    return convert(std::move(v), to, true);
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

    auto keepsQualifiers = [&](const Type *from, const Type *want) {
        for (;;) {
            if (from->isConst() && !want->isConst()) return false;
            from = from->unqualified();
            want = want->unqualified();
            if (!from->isPointer() || !want->isPointer()) return true;
            from = from->pointee();
            want = want->pointee();
        }
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
        if (!keepsQualifiers(v->type(), to->referent()))
            src_.fail(pos, "'reinterpret_cast<" + to->describe() + ">' of a '" +
                           v->type()->describe() + "' would take the const "
                           "off - 'const_cast' is what does that, and the two "
                           "are written separately on purpose");
        v->setType(to->referent());
        return v;
    }

    v = decay(std::move(v));
    const Type *from = v->type();

    const bool fromPointer = from->isPointer() || from->isNullPtr();
    if (to->isPointer() && fromPointer) {
        if (!keepsQualifiers(from, to))
            src_.fail(pos, "'reinterpret_cast<" + to->describe() + ">' of a '" +
                           from->describe() + "' would take the const off - "
                           "'const_cast' is what does that, and the two are "
                           "written separately on purpose");
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
        // `reinterpret_cast<int>(n)` where n is already an int. Legal, and
        // does nothing, which is what it should do.
        return v;
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
    if (peek().is("wchar_t")) return types_.get(target_.wcharType());
    return nullptr;
}

// `T(x)` and `T()` for a T that is not a class, the '(' still ahead.
ExprPtr Parser::bracedValueInit(const Type *to, std::size_t pos) {
    expect("{");
    if (!consume("}"))
        src_.fail(peek().pos, "'" + to->describe() + "{...}' with a value in the "
                              "braces is list-initialisation, which is not "
                              "supported yet - write the value in parentheses. "
                              "The empty pair is read: it value-initialises");
    if (to->isVoid()) src_.fail(pos, "'void{}' has no value");
    if (to->unqualified()->isStructOrUnion())
        src_.fail(pos, "'" + to->describe() + "{}' - value-initialising a class "
                       "with braces in an expression is not supported yet; "
                       "'" + to->describe() + "()' does the same thing and is "
                       "read");
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
    return convert(std::move(z), types_.withoutConst(to));
}

ExprPtr Parser::functionalCast(const Type *to, std::size_t pos) {
    expect("(");
    if (consume(")")) {
        if (to->isVoid())
            src_.fail(pos, "'void()' has no value");
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
        return convert(std::move(z), types_.withoutConst(to));
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
    return convert(std::move(v), types_.withoutConst(to));
}

// **A function's address, from its key.**
ExprPtr Parser::functionAsValue(const std::string &key, std::size_t pos) {
    if (const std::vector<std::size_t> *set = overloadsOf(key)) {
        if (set->size() > 1)
            src_.fail(pos, "'" + key + "' names " +
                           std::to_string(set->size()) + " functions, and "
                           "which one this is cannot be told from the use "
                           "alone - choosing an overload by the type it is "
                           "assigned to is not supported yet");
    }
    const Signature *sig = findFunction(key);
    if (sig == nullptr) return nullptr;
    Var *v = Var::global(key);
    v->setSymbol(sig->symbol);
    ExprPtr target(v);
    const Type *fn = types_.functionType(sig->returns, sig->params, sig->variadic);
    target->setType(fn);
    ExprPtr n(new Unary('&', std::move(target)));
    n->setType(types_.pointerTo(fn));
    return n;
}

ExprPtr Parser::primary(Program *program) {
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
        if (currentClass_ == nullptr)
            src_.fail(pos, "'this' is only inside a member function, and this "
                           "is not one");
        // [class.static]/1: a static member function has no `this`.
        if (inStaticMember_)
            src_.fail(pos, "'this' is not available in a static member "
                           "function - it is not called on an object, so there "
                           "is none to point at");
        const Local *slot = findLocal("this");
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
        const Signature &sig = resolveOverload(name, args, opos);
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
        const bool memberFirst =
            currentClass_ != nullptr && peekAt(1).is("(") &&
            findMemberOwner(currentClass_, peek().text) != nullptr;
        if (!memberFirst) return templateCall(program);
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
        if (!variadicBody_)
            src_.fail(pos, "va_arg is only allowed in a function declared "
                           "with '...'");
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
        case Kind::Short: case Kind::UShort: promotes = "int"; break;
        case Kind::Float:                    promotes = "double"; break;
        default: break;
        }
        if (promotes != nullptr)
            src_.fail(pos, "'" + want->describe() + "' is promoted before it "
                           "reaches a variadic function, so va_arg cannot ask "
                           "for it - ask for '" + promotes + "'");

        if (want->isStructOrUnion() || want->isArray())
            src_.fail(pos, "va_arg of an aggregate is not supported yet");

        ExprPtr n(new VaArg(std::move(list)));
        n->setType(want);
        return n;
    }

    if (consume("(")) {
        // Inside parentheses a `>` is an operator again, which is exactly why
        // C++ makes a comparison in a template argument need them.
        const bool wasInArgs = inTemplateArgs_;
        inTemplateArgs_ = false;
        ExprPtr e = expr();
        inTemplateArgs_ = wasInArgs;
        expect(")");
        return e;
    }

    if (peek().kind == TokenKind::Str) {
        std::string label = ".L.str." + std::to_string(strings_++);
        std::string text = peek().text;
        at_++;

        bool wide = tokens_[at_ - 1].wide;
        while (peek().kind == TokenKind::Str) {
            text += peek().text;

            wide = wide || peek().wide;
            at_++;
        }

        const Type *elem = wide ? types_.get(target_.wcharType())
                                : types_.charType();
        int width = elem->size(target_);

        std::string bytes;
        for (unsigned char ch : text) {
            bytes.push_back(static_cast<char>(ch));
            for (int k = 1; k < width; k++) bytes.push_back('\0');
        }
        for (int k = 0; k < width; k++) bytes.push_back('\0');

        program->strings.push_back(StringLit{ label, bytes, width });
        ExprPtr n(new StrLit(label, text));
        n->setType(types_.arrayOf(types_.withConst(elem),
                                  static_cast<long long>(text.size()) + 1));
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

        else if (t.wide)                 ty = types_.get(target_.wcharType());
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
        if (peekAt(typeEnd).is("(")) {
            std::string q = peek().text;
            for (std::size_t k = 1; k + 1 <= typeEnd - 1; k += 2)
                q += "::" + peekAt(k + 1).text;
            const Type *named = findTypedef(q);
            if (named != nullptr && named->isStructOrUnion()) {
                const std::size_t qpos = peek().pos;
                at_ += typeEnd + 1;                 // the name and the '('
                return classTemporary(named, qpos);
            }
        }
    }

    // **`::f()` - a name asked for at global scope explicitly.** Refused by
    // name rather than left to "expected an expression", which points at a
    // '::' and says nothing.
    if (peek().is("::"))
        src_.fail(peek().pos, "a name qualified with '::' alone is not "
                              "supported yet in an expression - as a type, "
                              "'::Lexer *p;' works. Write the name without it "
                              "where nothing shadows it");

    // ...and `N::S::n` is not this: the chain reaches a *class*, and what follows a
    // class is a member, which the branch below already knows how to find. Asked
    // before the namespace is eaten, since eating it loses the start of the name.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("::") &&
        namespaces_.find(peek().text) != namespaces_.end() &&
        // **...unless what follows the type is a `(`**, which makes it a
        // temporary rather than the qualifier of a member: `std::vector<T>()`
        // belongs to this branch, `std::vector<T>::size_type` does not.
        (qualifiedTypeEnd() == 0 ||
         peekAt(qualifiedTypeEndPastArgs()).is("("))) {
        const std::size_t qpos = peek().pos;
        std::string scope = peek().text;
        at_ += 2;
        while (peek().kind == TokenKind::Ident && peekAt(1).is("::") &&
               namespaces_.find(scope + "::" + peek().text) != namespaces_.end()) {
            scope += "::" + peek().text;
            at_ += 2;
        }
        // **`std::copy(a, b, c)` - a function template named qualified.**
        if (peek().kind == TokenKind::Ident && isTemplateName(peek().text, true))
            return templateCall(program);

        const std::string full = scope + "::" + expectIdent("a name");
        if (consume("(")) {
            std::vector<ExprPtr> args;
            parseArguments(args);
            const Signature &sig = resolveOverload(full, args, qpos);
            applyDefaults(sig, args, qpos);
            return completeCall(full, sig.symbol, nullptr, sig.returns,
                                sig.params, sig.variadic, qpos, std::move(args),
                                !sig.owner.empty());
        }
        if (ExprPtr v = objectRef(full)) return v;
        if (const EnumConst *e = findEnum(full)) {
            ExprPtr n(new Num(e->value));
            n->setType(types_.intType());
            return n;
        }
        // `take(N::nl)` - the function itself, written qualified and not called.
        if (ExprPtr f = functionAsValue(full, qpos)) return f;
        src_.fail(qpos, "'" + full + "' was not declared in '" + scope + "'");
    }

    if (peek().kind == TokenKind::Ident && peekAt(1).is("::") &&
        peekAt(2).kind == TokenKind::Ident) {
        // The longest prefix that names a class and has such a member wins, so
        // that `Outer::Inner::shared` finds Inner's rather than stopping at
        // Outer.
        std::string q = peek().text;
        const Type *owner = nullptr;
        std::string member;
        std::size_t consumed = 0;
        for (std::size_t k = 1; peekAt(k).is("::") &&
                                peekAt(k + 1).kind == TokenKind::Ident; k += 2) {
            const std::string component = peekAt(k + 1).text;
            if (const Type *cls = findTypedef(q))
                if (cls->isStructOrUnion() &&
                    cls->findStaticMember(component) != nullptr) {
                    owner = cls;
                    member = component;
                    consumed = k + 2;
                }
            q += "::" + component;
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
        const EnumConst *found = nullptr;
        std::size_t took = 0;
        for (std::size_t k = 1; peekAt(k).is("::") &&
                                peekAt(k + 1).kind == TokenKind::Ident; k += 2) {
            const std::string component = peekAt(k + 1).text;
            if (const Type *cls = findTypedef(e))
                if (cls->isStructOrUnion())
                    if (const EnumConst *c = enumInClass(cls, component)) {
                        found = c;
                        took = k + 2;
                    }
            e += "::" + component;
        }
        if (found != nullptr) {
            at_ += took;
            ExprPtr n(new Num(found->value));
            n->setType(types_.intType());
            return n;
        }
    }

    // **`S::f(...)` - a static member function, called with no object.** The
    // twin of the static *data* member found just above, and looked up the
    // same way.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("::")) {
        std::string q = peek().text;
        std::string key;
        std::size_t consumed = 0;
        for (std::size_t k = 1; peekAt(k).is("::") &&
                                peekAt(k + 1).kind == TokenKind::Ident; k += 2) {
            const std::string component = peekAt(k + 1).text;
            // **Through findTypedef, as the static *data* member walk above
            // does.**
            std::string candidate = q + "::" + component;
            if (const Type *cls = findTypedef(q))
                if (cls->isStructOrUnion())
                    candidate = cls->tag() + "::" + component;
            if (peekAt(k + 2).is("(") && hasStaticMemberNamed(candidate)) {
                key = candidate;
                consumed = k + 2;
            }
            q += "::" + component;
        }
        if (!key.empty()) {
            const std::size_t qpos = peek().pos;
            at_ += consumed;
            expect("(");
            std::vector<ExprPtr> args;
            parseArguments(args);
            const Signature &sig = resolveOverload(key, args, qpos);
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
        // Only a class this one *is* - itself or a base of it. Anything else is
        // some other class's member and has no object here to be called on.
        if (owner != nullptr &&
            currentClass_->unqualified() != owner->unqualified() &&
            publicBaseOffset(currentClass_, owner->unqualified()) < 0)
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
        const GlobalSym *g = l != nullptr ? nullptr : findGlobal(name);
        const Type *held = l != nullptr ? l->type : (g != nullptr ? g->type : nullptr);
        // A name that holds something callable rather than naming a function:
        // a function pointer, or an object whose `(` is [over.call].
        const Type *callee = held != nullptr && held->isReference()
                           ? held->referent() : held;
        bool callsThroughObject =
            callee != nullptr && (callee->isFunctionPointer() ||
                                  callee->unqualified()->isStructOrUnion());

        // **An unqualified static member, inside a member function or a lambda
        // written in one.** It needs no object; a nearer local or global won above.
        const Type *staticScopes[2] = { currentClass_, lambdaScope() };
        for (const Type *staticScope : staticScopes) {
            if (l != nullptr || g != nullptr || staticScope == nullptr ||
                peekAt(1).is("("))
                break;
            if (const Type::StaticMember *s = staticScope->findStaticMember(name)) {
                at_++;
                return staticMemberRef(staticScope, *s, staticScope->tag(), pos);
            }
        }

        // An unqualified call inside a member function looks for a member of this class first -
        // [class.mfct.non-static] makes `secret()` mean `this->secret()` - before the free-function
        // branch calls it undeclared. **A class's own name inside it is not a member function.**
        bool inherited = false;
        for (const Type *c = currentClass_; c != nullptr; c = c->base()) {
            if (name == localOf(c->tag())) break;
            if (overloadsOf(c->tag() + "::" + name) != nullptr) { inherited = true; break; }
        }
        // **A static member called by its bare name, from inside the class.**
        // [class.static]/1 makes it `C::f(...)` and not `this->f(...)`: the
        // object is not merely unused, it is absent.
        if (peekAt(1).is("(") && !callsThroughObject && currentClass_ != nullptr &&
            l == nullptr && g == nullptr) {
            std::string key;
            const Type *roots[2] = { currentClass_, lambdaScope() };
            for (const Type *root : roots)
                for (const Type *c = root; c != nullptr && key.empty(); c = c->base())
                    if (hasStaticMemberNamed(c->tag() + "::" + name))
                        key = c->tag() + "::" + name;
            if (!key.empty()) {
                at_ += 2;
                std::vector<ExprPtr> args;
                parseArguments(args);
                const Signature &sig = resolveOverload(key, args, pos);
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

        if (peekAt(1).is("(") && !callsThroughObject && currentClass_ != nullptr &&
            l == nullptr && g == nullptr && inherited) {
            if (const Local *self = findLocal("this")) {
                at_ += 2;
                ExprPtr me(Var::local("this", self->offset));
                me->setType(self->type);
                ExprPtr obj(new Unary('*', std::move(me)));
                obj->setType(self->type->pointee());
                return memberCall(std::move(obj), self->type->pointee(), name, pos);
            }
        }

        // `P(1)` where P names a class: a temporary, not a call to a function
        // nobody declared. Asked before the call branch below, which would
        // look the name up in the function table and report it undeclared.
        if (peekAt(1).is("{") && peekAt(2).is("}") && !callsThroughObject &&
            l == nullptr && g == nullptr) {
            if (const Type *named = findTypedef(name)) {
                at_++;
                return bracedValueInit(named, pos);
            }
        }

        if (peekAt(1).is("(") && !callsThroughObject && l == nullptr &&
            g == nullptr) {
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
            const Signature &sig = resolveOverload(name, args, pos);
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
            if (const Member *m = currentClass_->findMember(name)) {
                // **[class.access]/1 applies however the member is spelled**, and this path checked
                // nothing: `x` inside a derived class reached a private member of its base where
                // `this->x` and `d.x` were both refused.
                checkAccessible(currentClass_, *m, pos);
                if (self == nullptr)
                    src_.fail(pos, "'" + name + "' is a member and there is no "
                                   "object here to read it from");
                const Type *held = self->type->pointee();
                ExprPtr acc = thisMember(self->offset, held, *m);
                // The same two rules as the `.` and `->` paths: a const
                // object does not reach through a reference member, and a
                // reference member is read by dereferencing what it holds.
                acc->setType(held->isConst() && !m->type->isReference() &&
                             !m->isMutable
                                 ? types_.withConst(m->type) : m->type);
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
                    acc->setType(of->isConst() && !om->type->isReference() &&
                                 !om->isMutable
                                     ? types_.withConst(om->type) : om->type);
                    return useReference(std::move(acc));
                }
            }

            if (const EnumConst *e = findClassEnum(name)) {
                ExprPtr n(new Num(e->value));
                n->setType(types_.intType());
                return n;
            }
        }

        // Namespace scope, last of the three.
        if (const EnumConst *e = findEnum(name)) {
            ExprPtr n(new Num(e->value));
            n->setType(types_.intType());
            return n;
        }
        if (ExprPtr v = globalRef(name)) return v;

        // Taking the address of an overloaded name needs a target type to
        // choose by - [over.over] - and there is none here.
        if (ExprPtr f = functionAsValue(
                qualifyForLookup(name, &Parser::hasFunctionNamed), pos))
            return f;
        // **`S{...}` is list-initialisation written as an expression** - the
        // same rule a declaration's braces meet, one syntax over.
        if (peek().is("{") && findTypedef(name) != nullptr)
            src_.fail(pos, "'" + name + "{...}' is list-initialisation, and "
                           "that is not supported yet - '" + name +
                           "(...)' calls a constructor here, and a plain "
                           "struct is built by naming its members");
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
        if (peekAt(k).is(")")) return true;
        if (!peekAt(k).is(".") && !peekAt(k).is("->")) return false;
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

    // Unevaluated: [dcl.type.simple]/4. The operand is read for its type and
    // nothing in it happens, so nothing it registered may outlive the read.
    Discarded held(this);
    ExprPtr e = expr();
    expect(")");
    const Type *t = e->type();
    if (t == nullptr)
        src_.fail(pos, "'decltype' needs an expression with a type, and this "
                       "one has none");
    if (namePath) return t;
    // [dcl.type.simple]/4 in full: an xvalue answers `T &&`, an lvalue `T &`,
    // and a prvalue `T`. Three categories and three answers, which is why the
    // xvalue mark has to be asked about before the lvalue is.
    if (e->isXvalue()) return types_.rvalueReferenceTo(t);
    return isLvalue(*e) ? types_.referenceTo(t) : t;
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

// What is stored in a reference is an address, so binding one is taking the
// address of the initialiser. The rules here are the whole of what makes a
// reference different from a pointer that is always dereferenced.
ExprPtr Parser::bindReference(const Type *ref, ExprPtr init, std::size_t pos,
                              const std::string &what) {
    const Type *referent = ref->referent();
    const Type *it = init->type();

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

    // **An rvalue reference takes exactly what an lvalue one will not.**
    // [dcl.init.ref]: `T &&` binds to a value with nowhere to live and refuses an
    // object that has somewhere - which is how a move takes an object apart.
    if (ref->isRValueReference() && isLvalue(*init))
        src_.fail(pos, what + " is '" + ref->describe() + "' and this is an "
                       "object with an address of its own - an rvalue "
                       "reference binds only to a value that has none, so "
                       "that taking it apart harms nobody");

    // The direct binding: an addressable glvalue of exactly the type named,
    // which the reference then *is*.
    if (isGlvalue(*init) && noAddressBecause == nullptr &&
        it->unqualified() != referent->unqualified() &&
        publicBaseOffset(it, referent) > -1 && !ref->isRValueReference()) {
        if (it->isConst() && !referent->isConst())
            src_.fail(pos, what + " is '" + ref->describe() + "' and this is '" +
                           it->describe() + "' - a reference that can write "
                           "cannot bind to a const");
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
        if (!arrayBind && it->isConst() && !referent->isConst())
            src_.fail(pos, what + " is '" + ref->describe() + "' and this is '" +
                           it->describe() + "' - a reference that can write "
                           "cannot bind to a const");
        ExprPtr addr(new Unary('&', std::move(init)));
        addr->setType(types_.pointerTo(referent));
        return addr;
    }

    // Anything else needs a temporary to bind to, and only a const reference may
    // have one - [dcl.init.ref]/5. The refusals are separate: a type that does not
    // match, and a value with no address. An rvalue reference is allowed one.
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
    // **This copy is not registered for destruction, deliberately.**
    return both;
}

// **Block scope and namespace scope are separate questions**, because [basic.lookup.unqual] puts
// class scope between them: a member hides a global and is hidden by a local. objectRef asks both
// in order for the callers that are not inside a class and have no middle step to take.
ExprPtr Parser::objectRef(const std::string &name) {
    if (ExprPtr v = localRef(name)) return v;
    return globalRef(name);
}

ExprPtr Parser::localRef(const std::string &name) {
    if (const Local *l = findLocal(name)) {
        Var *v = l->staticName.empty() ? Var::local(name, l->offset)
                                       : Var::global(l->staticName);
        v->setReadOnly(l->isConst);
        v->setNoAddress(l->isRegister);
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
        ExprPtr n(v);
        n->setType(g->type);
        // A reference at file scope is a slot holding an address, as a local
        // one is, so a mention of it is a dereference of the slot.
        return useReference(std::move(n));
    }
    return nullptr;
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
            at_++;
            n = memberCall(std::move(n), self, "operator()", pos);
            continue;
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
                full.push_back(types_.pointerTo(types_.get(Kind::Void)));
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
            const Type *obj = n->type()->pointee();
            ExprPtr deref(new Unary('*', std::move(n)));
            deref->setType(obj);
            n = std::move(deref);
            std::string name = declaredName("a member name");
            // **A member function template**, `p->head<3>()` - told from `<` as
            // a comparison only because the member is a registered template.
            if ((peek().is("<") || peek().is("(")) && isMemberTemplate(obj, name)) {
                n = memberTemplateCall(std::move(n), obj, name, pos);
                continue;
            }
            // **A data member that is itself callable**: `p.H(i, j)` reaches H
            // and then applies its operator(), where `p.f(i)` calls a member
            // function f.
            if (findMemberOwner(obj->unqualified(), name) != nullptr &&
                consume("(")) {
                n = memberCall(std::move(n), obj, name, pos);
                continue;
            }
            // **`p->count` where count is static** names the one shared
            // object, and the expression on the left is still evaluated -
            // [expr.ref] says so - which is what the comma is for.
            if (const Type::StaticMember *s = obj->findStaticMember(name)) {
                ExprPtr one = staticMemberRef(obj, *s, obj->tag(), pos);
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
            const Member *m = obj->findMember(name);
            if (!m) src_.fail(pos, "'" + obj->describe() + "' has no member '" + name + "'");
            checkAccessible(obj, *m, pos);
            // A member of a virtual base is not at a constant offset. The test
            // comes first because the call consumes the object: asking
            // unconditionally moved `n` away from every ordinary access too.
            if (m->inVirtualBase != nullptr) {
                refuseVirtualBaseMember(obj, *m, name, pos);
                if (ExprPtr viaVb = virtualBaseMember(std::move(n), obj, *m)) {
                    n = std::move(viaVb);
                    continue;
                }
            }
            ExprPtr acc(new MemberAccess(std::move(n), name, m->offset,
                                         m->width, m->bitOffset));
            // A member reached through a const object is itself const - [expr.ref]
            // gives it the object's qualification. **But not a reference member's
            // referent**: [dcl.ref] stops the const at the reference itself.
            acc->setType(obj->isConst() && !m->type->isReference() &&
                         !m->isMutable
                             ? types_.withConst(m->type) : m->type);
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
            const Type *obj = n->type();
            std::string name = declaredName("a member name");
            // **A member function template**, `v.head<3>()`.
            if ((peek().is("<") || peek().is("(")) && isMemberTemplate(obj, name)) {
                n = memberTemplateCall(std::move(n), obj, name, pos);
                continue;
            }
            // **A data member that is itself callable**: `p.H(i, j)` reaches H
            // and then applies its operator(), where `p.f(i)` calls a member
            // function f.
            if (findMemberOwner(obj->unqualified(), name) != nullptr &&
                consume("(")) {
                n = memberCall(std::move(n), obj, name, pos);
                continue;
            }
            // **`p->count` where count is static** names the one shared
            // object, and the expression on the left is still evaluated -
            // [expr.ref] says so - which is what the comma is for.
            if (const Type::StaticMember *s = obj->findStaticMember(name)) {
                ExprPtr one = staticMemberRef(obj, *s, obj->tag(), pos);
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
            const Member *m = obj->findMember(name);
            if (!m) src_.fail(pos, "'" + obj->describe() + "' has no member '" + name + "'");
            checkAccessible(obj, *m, pos);
            // A member of a virtual base is not at a constant offset. The test
            // comes first because the call consumes the object: asking
            // unconditionally moved `n` away from every ordinary access too.
            if (m->inVirtualBase != nullptr) {
                refuseVirtualBaseMember(obj, *m, name, pos);
                if (ExprPtr viaVb = virtualBaseMember(std::move(n), obj, *m)) {
                    n = std::move(viaVb);
                    continue;
                }
            }
            ExprPtr acc(new MemberAccess(std::move(n), name, m->offset,
                                         m->width, m->bitOffset));
            // A member reached through a const object is itself const - [expr.ref]
            // gives it the object's qualification. **But not a reference member's
            // referent**: [dcl.ref] stops the const at the reference itself.
            acc->setType(obj->isConst() && !m->type->isReference() &&
                         !m->isMutable
                             ? types_.withConst(m->type) : m->type);
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
        return v;
    }

    if (peek().is("++") || peek().is("--")) {
        bool inc = peek().is("++");
        at_++;
        return incDec(unary(), inc, true, pos);
    }
    if (consume("~")) {
        ExprPtr v = castExpr();
        if (ExprPtr call = overloadedUnary("~", v, pos)) return call;
        v = decay(std::move(v));
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
        v = decay(std::move(v));
        if (!v->type()->isArithmetic())
            src_.fail(pos, "unary '-' needs a number, not '" + v->type()->describe() + "'");
        const Type *t = promote(v->type());
        ExprPtr n(new Unary('-', convert(std::move(v), t)));
        n->setType(t);
        return n;
    }
    if (consume("&")) {
        // **`&S::x` - a pointer to a member, which is an offset and not an address.**
        // Read here because nothing else would: `primary`'s qualified-name path
        // answers for a *static* member, and a non-static one has no address.
        if (peek().kind == TokenKind::Ident && peekAt(1).is("::") &&
            peekAt(2).kind == TokenKind::Ident) {
            if (const Type *cls = findTypedef(peek().text))
                if (cls->isStructOrUnion()) {
                    if (const Member *m = cls->findMember(peekAt(2).text)) {
                        checkAccessible(cls, *m, pos);
                        at_ += 3;
                        ExprPtr off(new Num(static_cast<long long>(m->offset)));
                        off->setType(types_.memberPointerTo(cls, m->type));
                        return off;
                    }
                    // `&S::f` for a member function: the ABI's pair, built in
                    // a slot of this frame the way a class temporary is.
                    const std::string key = cls->tag() + "::" + peekAt(2).text;
                    if (const std::vector<std::size_t> *set = overloadsOf(key)) {
                        if (set->size() > 1)
                            src_.fail(pos, "'" + key + "' names " +
                                           std::to_string(set->size()) +
                                           " functions, and which one this is "
                                           "cannot be told from the use alone");
                        const Signature &f = functions_[(*set)[0]];
                        // **A virtual one is refused by name.** Itanium keeps the
                        // vtable index in the low bit and branches on it at every
                        // call; Microsoft calls a thunk. Neither is written.
                        if (f.isVirtual)
                            src_.fail(pos, "'" + key + "' is virtual, and a "
                                           "pointer to a virtual member "
                                           "function is not supported yet - it "
                                           "holds a vtable index where this "
                                           "holds an address");
                        at_ += 3;
                        return boundMemberPointer(cls, f, pos);
                    }
                }
        }
        ExprPtr v = castExpr();
        // Only when the class declared one.
        if (ExprPtr call = overloadedUnary("&", v, pos)) return call;
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
        v = decay(std::move(v));
        if (!v->type()->isPointer())
            src_.fail(pos, "'*' needs a pointer, not '" + v->type()->describe() + "'");

        if (v->type()->pointee()->isFunction()) return v;
        const Type *elem = v->type()->pointee();
        if (elem->isVoid()) src_.fail(pos, "'void *' cannot be dereferenced");
        ExprPtr n(new Unary('*', std::move(v)));
        n->setType(elem);
        return n;
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
        (void) expr();
        const bool quiet = mayThrow_ == 0;
        mayThrow_ = outer;
        expect(")");
        ExprPtr n(new Num(static_cast<long long>(quiet ? 1 : 0)));
        n->setType(types_.get(Kind::Bool));
        return n;
    }

    if (peek().is("sizeof")) {
        at_++;
        const Type *measured = nullptr;
        if (peek().is("(") && [this] {
                std::size_t save = at_; at_++; bool t = atTypeName(); at_ = save; return t;
            }()) {
            at_++;
            StorageClass sc;
            measured = specifiers(&sc);
            measured = declarator(measured, true).type;
            expect(")");
        } else {
            // Unevaluated: [expr.sizeof]/1. A call in here registers a result
            // slot for destruction and no object is ever built in it.
            Discarded held(this);
            ExprPtr operand = unary();
            if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(operand.get()))
                if (m->isBitField())
                    src_.fail(pos, "sizeof cannot be applied to '" + m->name() +
                                   "', which is a bit-field");
            measured = operand->type();
        }
        // **A signature that depends on its parameters through an *expression*
        // cannot be given a name.** Itanium spells such a pattern as the expression
        // itself - measured with clang - and nothing here can write that.
        if (patternOnly_ && (measured->kind() == Kind::TemplateParam ||
                             measured->kind() == Kind::DependentMember))
            src_.fail(pos, "'sizeof' of a template parameter in a signature is "
                           "not supported yet - the linker name would have to "
                           "spell the expression, and that is its own step");
        if (!measured->isComplete())
            src_.fail(pos, "sizeof needs a complete type");
        ExprPtr n(new Num(static_cast<long long>(measured->size(target_))));
        n->setType(types_.get(target_.sizeType()));
        return n;
    }
    return postfix();
}
