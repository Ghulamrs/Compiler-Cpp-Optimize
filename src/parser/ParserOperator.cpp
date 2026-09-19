// The parser: the operator ladder. One function per precedence level, from the
// cast expression up to the comma in the order [expr] gives them, plus the
// compound assignments and the increments written in terms of them.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>

ExprPtr Parser::castExpr() {
    if (peek().is("(")) {
        std::size_t save = at_;
        const std::size_t openPos = peek().pos;
        at_++;
        if (atTypeName()) {
            // **`(T(2.5) == ...)` is not a cast to a function type.**
            bool isCast = false;
            try {
                Trial trial(this);
                StorageClass psc;
                const Type *pt = specifiers(&psc);
                declarator(pt, true);
                isCast = peek().is(")");
            } catch (const SubstitutionFailure &f) {
                // **A refusal that names a feature is an answer, not a no.**
                // The trial is asking whether this parenthesis holds a
                // type-id.
                if (f.unsupported) src_.fail(f.pos, f.why);
                isCast = false;
            }
            if (isCast) {
                StorageClass sc;
                const Type *to = specifiers(&sc);
                to = declarator(to, true).type;
                expect(")");
                ExprPtr v = decay(castExpr());
                if (to->isVoid()) return ExprPtr(new Cast(to, std::move(v)));
                refuseUnrelatedClassCast(*v, to, openPos, "a cast");
                return convert(std::move(v), to, true);   // a cast allows explicit
            }
        }
        at_ = save;
    }
    return unary();
}

// `&S::f` - the pair the ABI keeps, built into a slot of this frame: the
// function's address, and on Itanium a `this` adjustment which is zero for every
// case here. Shaped as classTemporary's is, a dereference of a comma.
ExprPtr Parser::boundMemberPointer(const Type *cls, const Signature &f,
                                   std::size_t pos, long long vtableCode,
                                   const std::string &code) {
    const Type *fn = types_.functionType(f.returns, f.params, f.variadic);
    const Type *mp = types_.memberFunctionPointerTo(cls, fn, target_);
    const int slot = allocateFrameSlot(mp);
    const Type *word = types_.pointerTo(types_.get(Kind::Void));

    ExprPtr asWord;
    if (vtableCode != 0) {
        // The slot's offset with the low bit set, which is what the call
        // tests for; no function is named at all.
        asWord.reset(new Num(vtableCode));
        asWord->setType(ptrdiffType());
        ExprPtr made(new Cast(word, std::move(asWord)));
        made->setType(word);
        asWord = std::move(made);
    } else {
        Var *fnVar = Var::global(f.name);
        fnVar->setSymbol(code.empty() ? f.symbol : code);
        ExprPtr target(fnVar);
        target->setType(fn);
        ExprPtr addr(new Unary('&', std::move(target)));
        addr->setType(types_.pointerTo(fn));
        asWord.reset(new Cast(word, std::move(addr)));
        asWord->setType(word);
    }

    ExprPtr obj(Var::local("$mfp", slot));
    obj->setType(mp);
    const Member *fnSlot = mp->findMember("$fn");
    ExprPtr dst(new MemberAccess(std::move(obj), "$fn", fnSlot->offset, 0, 0));
    dst->setType(word);
    ExprPtr store(new Assign(std::move(dst), std::move(asWord)));
    store->setType(word);

    ExprPtr chain = std::move(store);
    // The adjustment, and Microsoft's vbtable index: zero, for a member of
    // the class itself, which is every `&S::f` this reads.
    static const char *const zeroed[] = { "$adj", "$vbi" };
    for (std::size_t zi = 0; zi < 2; zi++) {
        const Member *adj = mp->findMember(zeroed[zi]);
        if (adj == nullptr) continue;
        ExprPtr self(Var::local("$mfp", slot));
        self->setType(mp);
        ExprPtr zeroAt(new MemberAccess(std::move(self), zeroed[zi], adj->offset,
                                        0, 0));
        zeroAt->setType(adj->type);
        ExprPtr zero(new Num(0LL));
        zero->setType(adj->type);
        ExprPtr put(new Assign(std::move(zeroAt), std::move(zero)));
        put->setType(adj->type);
        ExprPtr both(new Comma(std::move(chain), std::move(put)));
        both->setType(adj->type);
        chain = std::move(both);
    }

    ExprPtr whole(Var::local("$mfp", slot));
    whole->setType(mp);
    ExprPtr at(new Unary('&', std::move(whole)));
    at->setType(types_.pointerTo(mp));
    ExprPtr seq(new Comma(std::move(chain), std::move(at)));
    seq->setType(types_.pointerTo(mp));
    ExprPtr made(new Unary('*', std::move(seq)));
    made->setType(mp);
    (void)pos;
    return made;
}

ExprPtr Parser::applyMemberPointer(ExprPtr addr, ExprPtr mp, std::size_t pos,
                                   bool constObject) {
    const Type *mpt = mp->type()->unqualified();
    // **[expr.mptr.oper]/3: the object is converted to the member pointer's
    // class first** - a `&B2::f` applied to a D whose B2 is not at 0 moves
    // `this` to that base before anything the pair or the offset says.
    if ((mpt->isMemberFunctionPointer() || mpt->isMemberPointer()) &&
        mpt->enclosing() != nullptr && addr->type()->isPointer()) {
        const Type *owner = mpt->enclosing()->unqualified();
        const Type *have = addr->type()->pointee();
        if (have->unqualified() != owner) {
            const Type *to = have->isConst() ? types_.withConst(owner) : owner;
            addr = convert(std::move(addr), types_.pointerTo(to));
        }
    }
    // A pointer to a member *function*: read the code pointer out of it and
    // leave the object's address for the call to pick up.
    if (mpt->isMemberFunctionPointer()) {
        const Member *slot = mpt->findMember("$fn");
        const Type *fnPtr = types_.pointerTo(mpt->pointee());
        boundFn_ = mpt->pointee();
        boundAt_ = pos;
        if (target_.microsoftNames() && mpt->findMember("$adj") == nullptr) {
            // One code pointer, a vcall thunk standing in for a virtual one.
            ExprPtr held(new MemberAccess(std::move(mp), "$fn", slot->offset, 0, 0));
            held->setType(fnPtr);
            boundThis_ = std::move(addr);
            return held;
        }
        if (target_.microsoftNames()) {
            // cl's multiple and virtual forms carry a `this` adjustment after the
            // code pointer, and a cl caller may hand one that is not zero: the
            // pair is copied to a slot and the object moved by it, as Itanium's is.
            const Type *thisType = addr->type();
            const Type *chars = types_.pointerTo(types_.get(Kind::Char));
            const Member *adjSlot = mpt->findMember("$adj");
            const int pairSlot = allocateFrameSlot(mpt);
            const std::string pairName = ".mp" + std::to_string(refTemps_++);
            auto pair = [&]() { ExprPtr e(Var::local(pairName, pairSlot)); e->setType(mpt); return e; };
            ExprPtr keepPair(new Assign(pair(), std::move(mp)));
            keepPair->setType(mpt);
            ExprPtr adjRead(new MemberAccess(pair(), "$adj", adjSlot->offset, 0, 0));
            adjRead->setType(adjSlot->type);
            ExprPtr asChars(new Cast(chars, std::move(addr)));
            asChars->setType(chars);
            ExprPtr moved(new Binary(BinOp::Add, std::move(asChars), std::move(adjRead)));
            moved->setType(chars);
            ExprPtr self(new Cast(thisType, std::move(moved)));
            self->setType(thisType);
            ExprPtr code(new MemberAccess(pair(), "$fn", slot->offset, 0, 0));
            code->setType(fnPtr);
            ExprPtr callee(new Comma(std::move(keepPair), std::move(code)));
            callee->setType(fnPtr);
            boundThis_ = std::move(self);
            return callee;
        }
        // **Itanium decides at the call**: the pair copied to a slot, the
        // object moved by its adjustment, and a set low bit in the code word
        // means "vtable offset + 1", read through the vptr; clear, an address.
        const Type *thisType = addr->type();
        const Type *chars = types_.pointerTo(types_.get(Kind::Char));
        const Type *diff = ptrdiffType();
        const Member *adjSlot = mpt->findMember("$adj");
        const int pairSlot = allocateFrameSlot(mpt);
        const int thisSlot = allocateFrameSlot(chars);
        const std::string pairName = ".mp" + std::to_string(refTemps_++);
        const std::string thisName = ".mt" + std::to_string(refTemps_++);
        auto pair = [&]() { ExprPtr e(Var::local(pairName, pairSlot)); e->setType(mpt); return e; };
        auto pairField = [&](const Member *m) {
            ExprPtr e(new MemberAccess(pair(), m->name, m->offset, 0, 0));
            e->setType(m->type); return e;
        };
        auto movedThis = [&]() { ExprPtr e(Var::local(thisName, thisSlot)); e->setType(chars); return e; };

        ExprPtr keepPair(new Assign(pair(), std::move(mp)));
        keepPair->setType(mpt);
        ExprPtr asChars(new Cast(chars, std::move(addr)));
        asChars->setType(chars);
        ExprPtr moved(new Binary(BinOp::Add, std::move(asChars), pairField(adjSlot)));
        moved->setType(chars);
        ExprPtr keepThis(new Assign(movedThis(), std::move(moved)));
        keepThis->setType(chars);

        ExprPtr codeWord(new Cast(diff, pairField(slot)));
        codeWord->setType(diff);
        ExprPtr one(new Num(1LL));
        one->setType(diff);
        ExprPtr low(new Binary(BinOp::BitAnd, std::move(codeWord), std::move(one)));
        low->setType(diff);

        ExprPtr vptrAt(new Cast(types_.pointerTo(chars), movedThis()));
        vptrAt->setType(types_.pointerTo(chars));
        ExprPtr vptr(new Unary('*', std::move(vptrAt)));
        vptr->setType(chars);
        ExprPtr codeAgain(new Cast(diff, pairField(slot)));
        codeAgain->setType(diff);
        ExprPtr minusOne(new Num(-1LL));
        minusOne->setType(diff);
        ExprPtr offset(new Binary(BinOp::Add, std::move(codeAgain), std::move(minusOne)));
        offset->setType(diff);
        ExprPtr entryAt(new Binary(BinOp::Add, std::move(vptr), std::move(offset)));
        entryAt->setType(chars);
        ExprPtr entryPtr(new Cast(types_.pointerTo(fnPtr), std::move(entryAt)));
        entryPtr->setType(types_.pointerTo(fnPtr));
        ExprPtr fromTable(new Unary('*', std::move(entryPtr)));
        fromTable->setType(fnPtr);
        ExprPtr direct(new Cast(fnPtr, pairField(slot)));
        direct->setType(fnPtr);
        ExprPtr chosen(new Conditional(std::move(low), std::move(fromTable), std::move(direct)));
        chosen->setType(fnPtr);

        ExprPtr seq(new Comma(std::move(keepPair), std::move(keepThis)));
        seq->setType(chars);
        ExprPtr callee(new Comma(std::move(seq), std::move(chosen)));
        callee->setType(fnPtr);
        ExprPtr self(new Cast(thisType, movedThis()));
        self->setType(thisType);
        boundThis_ = std::move(self);
        return callee;
    }
    if (!mpt->isMemberPointer())
        src_.fail(pos, "the right of '.*' has to be a pointer to a member, and "
                       "this is '" + mp->type()->describe() + "'");
    const Type *member = mpt->pointee();
    const Type *bytes = types_.pointerTo(types_.get(Kind::Char));

    ExprPtr asBytes(new Cast(bytes, std::move(addr)));
    asBytes->setType(bytes);
    ExprPtr sum(new Binary(BinOp::Add, std::move(asBytes), std::move(mp)));
    sum->setType(bytes);
    const Type *to = types_.pointerTo(member);
    ExprPtr at(new Cast(to, std::move(sum)));
    at->setType(to);
    ExprPtr read(new Unary('*', std::move(at)));
    // **[expr.mptr.oper]/6: the result is const where the object is.** cxx1 handed back the
    // member's own type, so `(s.*p) = 3;` wrote through a `const S` with no diagnostic - the same
    // hole V-09 was on the ordinary member path, one operator over.
    read->setType(constObject && !member->isReference()
                      ? types_.withConst(member) : member);
    return read;
}

// [expr.mptr.oper]: `.*` and `->*` bind tighter than a multiplication and
// looser than a cast, which is the level this sits at.
ExprPtr Parser::memberPointerExpr() {
    ExprPtr n = castExpr();
    for (;;) {
        std::size_t pos = peek().pos;
        if (consume(".*")) {
            const Type *of = n->type();
            if (!of->unqualified()->isStructOrUnion())
                src_.fail(pos, "the left of '.*' has to be an object of class "
                               "type, and this is '" + of->describe() + "'");
            const bool constObject = of->isConst();
            ExprPtr addr(new Unary('&', std::move(n)));
            addr->setType(types_.pointerTo(of->unqualified()));
            n = applyMemberPointer(std::move(addr), memberPointerExpr(), pos,
                                   constObject);
            continue;
        }
        if (consume("->*")) {
            const Type *of = n->type();
            if (!of->isPointer() ||
                !of->pointee()->unqualified()->isStructOrUnion())
                src_.fail(pos, "the left of '->*' has to be a pointer to a "
                               "class, and this is '" + of->describe() + "'");
            const bool constPointee = of->pointee()->isConst();
            n = applyMemberPointer(decay(std::move(n)), memberPointerExpr(), pos,
                                   constPointee);
            continue;
        }
        return n;
    }
}

ExprPtr Parser::mul() {
    ExprPtr n = memberPointerExpr();
    for (;;) {
        std::size_t pos = peek().pos;
        if (consume("*"))      n = arithmetic(BinOp::Mul, std::move(n), memberPointerExpr(), pos);
        else if (consume("/")) n = arithmetic(BinOp::Div, std::move(n), memberPointerExpr(), pos);
        else if (consume("%")) n = arithmetic(BinOp::Mod, std::move(n), memberPointerExpr(), pos);
        else return n;
    }
}

ExprPtr Parser::add() {
    ExprPtr n = mul();
    for (;;) {
        std::size_t pos = peek().pos;
        if (consume("+"))      n = arithmetic(BinOp::Add, std::move(n), mul(), pos);
        else if (consume("-")) n = arithmetic(BinOp::Sub, std::move(n), mul(), pos);
        else return n;
    }
}

ExprPtr Parser::shift() {
    ExprPtr n = add();
    for (;;) {
        BinOp op;
        if (inTemplateArgs_ && peek().is(">>")) return n;
        std::size_t pos = peek().pos;
        if (consume("<<"))      op = BinOp::Shl;
        else if (consume(">>")) op = BinOp::Shr;
        else return n;

        n = shiftOf(op, std::move(n), add(), pos);
    }
}

ExprPtr Parser::relational() {
    ExprPtr n = shift();
    for (;;) {
        // [temp.names]: a `>` inside a template argument list closes it. This
        // is the whole reason C++ makes `f<(a > b)>` need its parentheses,
        // and the parentheses are where the flag is cleared.
        if (inTemplateArgs_ && (peek().is(">") || peek().is(">>"))) return n;
        std::size_t pos = peek().pos;
        if (consume("<"))       n = comparison(BinOp::Lt, std::move(n), shift(), pos);
        else if (consume("<=")) n = comparison(BinOp::Le, std::move(n), shift(), pos);
        else if (consume(">"))  n = comparison(BinOp::Gt, std::move(n), shift(), pos);
        else if (consume(">=")) n = comparison(BinOp::Ge, std::move(n), shift(), pos);
        else return n;
    }
}

ExprPtr Parser::equality() {
    ExprPtr n = relational();
    for (;;) {
        std::size_t pos = peek().pos;
        if (consume("=="))      n = comparison(BinOp::Eq, std::move(n), relational(), pos);
        else if (consume("!=")) n = comparison(BinOp::Ne, std::move(n), relational(), pos);
        else return n;
    }
}

ExprPtr Parser::bitAnd() {
    ExprPtr n = equality();
    while (peek().is("&")) {
        std::size_t pos = peek().pos; at_++;
        n = arithmetic(BinOp::BitAnd, std::move(n), equality(), pos);
    }
    return n;
}

ExprPtr Parser::bitXor() {
    ExprPtr n = bitAnd();
    while (peek().is("^")) {
        std::size_t pos = peek().pos; at_++;
        n = arithmetic(BinOp::BitXor, std::move(n), bitAnd(), pos);
    }
    return n;
}

ExprPtr Parser::bitOr() {
    ExprPtr n = bitXor();
    while (peek().is("|")) {
        std::size_t pos = peek().pos; at_++;
        n = arithmetic(BinOp::BitOr, std::move(n), bitXor(), pos);
    }
    return n;
}

ExprPtr Parser::logicalAnd() {
    ExprPtr n = bitOr();
    while (peek().is("&&")) {
        std::size_t pos = peek().pos;
        at_++;
        const std::size_t before = pendingTemps_.size();
        ExprPtr r = decay(bitOr());
        n = decay(std::move(n));
        n = contextualScalar(std::move(n), pos, "'&&'");
        r = contextualScalar(std::move(r), pos, "'&&'");
        r = markSkippableTemporaries(std::move(r), before,
                                     pendingTemps_.size());
        ExprPtr node(new Binary(BinOp::LAnd, std::move(n), std::move(r)));
        node->setType(types_.get(Kind::Bool));   // [expr.log.and]/1
        n = std::move(node);
    }
    return n;
}

ExprPtr Parser::logicalOr() {
    ExprPtr n = logicalAnd();
    while (peek().is("||")) {
        std::size_t pos = peek().pos;
        at_++;
        const std::size_t before = pendingTemps_.size();
        ExprPtr r = decay(logicalAnd());
        n = decay(std::move(n));
        n = contextualScalar(std::move(n), pos, "'||'");
        r = contextualScalar(std::move(r), pos, "'||'");
        r = markSkippableTemporaries(std::move(r), before,
                                     pendingTemps_.size());
        ExprPtr node(new Binary(BinOp::LOr, std::move(n), std::move(r)));
        node->setType(types_.get(Kind::Bool));   // [expr.log.or]/1
        n = std::move(node);
    }
    return n;
}

ExprPtr Parser::clonePure(const Expr &e) {
    if (const Num *n = dynamic_cast<const Num *>(&e)) {

        ExprPtr c(n->type() && n->type()->isFloating() ? new Num(n->dvalue())
                                                      : new Num(n->value()));
        c->setType(n->type());
        return c;
    }
    if (const Var *v = dynamic_cast<const Var *>(&e)) {
        Var *raw = v->isLocal() ? Var::local(v->name(), v->offset())
                                : Var::global(v->name());
        // **The copy carries the linker's name too.** A global's `name()` is what
        // the programmer wrote and `symbol()` what the object file says; the clone
        // kept only the first, so `++n` linked against a symbol nothing defines.
        raw->setSymbol(v->symbol());
        raw->setReadOnly(v->readOnly());
        raw->setNoAddress(v->noAddress());
        ExprPtr c(raw);
        c->setType(v->type());
        return c;
    }
    if (const StrLit *s = dynamic_cast<const StrLit *>(&e)) {
        ExprPtr c(new StrLit(s->label(), s->text()));
        c->setType(s->type());
        return c;
    }
    if (const Cast *k = dynamic_cast<const Cast *>(&e)) {

        ExprPtr inner = clonePure(k->value());
        if (!inner) return nullptr;
        return ExprPtr(new Cast(k->type(), std::move(inner)));
    }
    if (const Unary *u = dynamic_cast<const Unary *>(&e)) {
        ExprPtr inner = clonePure(u->operand());
        if (!inner) return nullptr;
        ExprPtr c(new Unary(u->op(), std::move(inner)));
        c->setType(u->type());
        return c;
    }
    if (const Binary *b = dynamic_cast<const Binary *>(&e)) {
        ExprPtr l = clonePure(b->lhs());
        if (!l) return nullptr;
        ExprPtr r = clonePure(b->rhs());
        if (!r) return nullptr;
        ExprPtr c(new Binary(b->op(), std::move(l), std::move(r)));
        c->setType(b->type());
        return c;
    }
    if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(&e)) {
        ExprPtr obj = clonePure(m->object());
        if (!obj) return nullptr;
        ExprPtr c(new MemberAccess(std::move(obj), m->name(), m->offset(),
                                   m->width(), m->bitOffset()));
        c->setType(m->type());
        return c;
    }
    return nullptr;
}

ExprPtr Parser::cloneLvalue(const Expr &e, std::size_t pos) {
    if (ExprPtr copy = clonePure(e)) return copy;
    src_.fail(pos, "the left of a compound assignment is read and then written, "
                   "so it is evaluated twice, and this one has an effect that "
                   "cannot happen twice - give the subscript or the call a name "
                   "first, or write it out as 'x = x op e'");
}

ExprPtr Parser::shiftOf(BinOp op, ExprPtr lhs, ExprPtr rhs, std::size_t pos) {
    if (ExprPtr call = overloadedBinary(op, lhs, rhs, pos)) return call;
    const Type *lt = promote(lhs->type());
    const Type *rt = promote(rhs->type());
    ExprPtr n(new Binary(op, convert(std::move(lhs), lt),
                             convert(std::move(rhs), rt)));
    n->setType(lt);
    return n;
}

void Parser::requireAssignable(const Expr &e, std::size_t pos, const char *what) {
    if (!isLvalue(e))
        src_.fail(pos, std::string(what) + " is not something that can be assigned to");
    if (e.type()->isArray())
        src_.fail(pos, "an array cannot be assigned to");
    if (const Var *v = dynamic_cast<const Var *>(&e))
        if (v->readOnly())
            src_.fail(pos, "'" + v->name() + "' is const and cannot be assigned to");
    // Reaching a const through a pointer or a member is the case the
    // read-only flag on the object cannot see: nothing here is a named
    // object, and the only record that it may not be written is its type.
    if (e.type()->isConst())
        src_.fail(pos, std::string(what) + " is '" + e.type()->describe() +
                       "', and a const cannot be assigned to");
}

ExprPtr Parser::compound(BinOp op, ExprPtr target, ExprPtr value, std::size_t pos) {
    requireAssignable(*target, pos, "the left of a compound assignment");
    const Type *to = target->type();

    // **`a += b` is not `a = a + b` when a is a class.** The rewrite below is
    // for a built-in operand; [over.match.oper] gives a class `operator+=`
    // alone, member or non-member, ranked as one set the way `a + b`'s are.
    const bool leftClass = to->unqualified()->isStructOrUnion();
    const bool rightClass = value->type()->unqualified()->isStructOrUnion();
    if (leftClass || rightClass) {
        const std::string name = std::string("operator") + binOpSpelling(op) + "=";
        switch (resolveOperator(name, *target, value.get(), pos)) {
        case OperatorChoice::Member: {
            std::vector<ExprPtr> args;
            args.push_back(std::move(value));
            const Type *objectType = target->type();
            return memberCallWith(std::move(target), objectType, name, pos,
                                  std::move(args));
        }
        case OperatorChoice::NonMember: {
            std::vector<ExprPtr> args;
            args.push_back(std::move(target));
            args.push_back(std::move(value));
            const Signature &sig = resolveOverload(name, args, pos);
            return completeCall(name, sig.symbol, nullptr, sig.returns,
                                sig.params, sig.variadic, pos, std::move(args),
                                !sig.owner.empty());
        }
        case OperatorChoice::None:
            break;
        }
        // A class on the left has nowhere else to go: the built-in `@=` wants
        // an arithmetic lvalue, and a class is never one.
        if (leftClass)
            src_.fail(pos, "'" + to->unqualified()->describe() + "' declares no "
                           "'" + name + "' that takes this, and no non-member "
                           "one takes it either - a compound assignment on a "
                           "class is that operator alone: it is not rewritten "
                           "into '" + binOpSpelling(op) + "' and an assignment "
                           "the way it is for a built-in type");
        // **A class on the right reaches the built-in `@=` through its
        // conversion function**, [over.match.oper]/9 with [over.built]. Converted
        // here, so the rewrite below never goes looking for an `operator+`.
        const Type *rt = value->type();
        const Signature *conv = soleNumericConversion(rt);
        if (conv == nullptr)
            src_.fail(pos, "'" + to->describe() + "' cannot take '" +
                           rt->describe() + "' with '" + binOpSpelling(op) +
                           "=' - no '" + name + "' is declared for the pair, and "
                           "the built-in one would need the class to convert to "
                           "a number, which it does not");
        std::vector<ExprPtr> none;
        value = memberCallWith(std::move(value), rt, conv->name, pos,
                               std::move(none));
    }

    if (ExprPtr readBack = clonePure(*target)) {
        ExprPtr combined = (op == BinOp::Shl || op == BinOp::Shr)
            ? shiftOf(op, std::move(readBack), std::move(value), pos)
            : arithmetic(op, std::move(readBack), std::move(value), pos);
        ExprPtr node(new Assign(std::move(target), convert(std::move(combined), to)));
        node->setType(to);
        return node;
    }

    if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(target.get()))
        if (m->isBitField())
            src_.fail(pos, "'" + m->name() + "' is a bit-field, so it has no "
                           "address to take - and the object it is reached "
                           "through has an effect that cannot happen twice; "
                           "give that object a name first");

    const Type *ptr = types_.pointerTo(to);
    int slot = allocateFrameSlot(ptr);

    const std::string hidden = "$compound";

    ExprPtr addr(new Unary('&', std::move(target)));
    addr->setType(ptr);
    ExprPtr slotVar(Var::local(hidden, slot));
    slotVar->setType(ptr);
    ExprPtr save(new Assign(std::move(slotVar), std::move(addr)));
    save->setType(ptr);

    ExprPtr through[2];
    for (int k = 0; k < 2; k++) {
        ExprPtr v(Var::local(hidden, slot));
        v->setType(ptr);
        ExprPtr d(new Unary('*', std::move(v)));
        d->setType(to);
        through[k] = std::move(d);
    }

    ExprPtr combined = (op == BinOp::Shl || op == BinOp::Shr)
        ? shiftOf(op, std::move(through[0]), std::move(value), pos)
        : arithmetic(op, std::move(through[0]), std::move(value), pos);
    ExprPtr store(new Assign(std::move(through[1]), convert(std::move(combined), to)));
    store->setType(to);

    ExprPtr node(new Comma(std::move(save), std::move(store)));
    node->setType(to);
    return node;
}

ExprPtr Parser::incDec(ExprPtr target, bool increment, bool prefix, std::size_t pos) {
    // **The dummy `int` is how the standard tells the two apart**, and it is a
    // real parameter: [over.inc] gives the postfix form an extra int and passes 0
    // in it, so postfix is the ordinary two-operand resolution and prefix the one.
    if (target->type()->unqualified()->isStructOrUnion()) {
        const char *spelling = increment ? "++" : "--";
        const std::string name = std::string("operator") + spelling;
        const Type *self = target->type();

        ExprPtr dummy;
        if (!prefix) {
            dummy.reset(new Num(0LL));
            dummy->setType(types_.intType());
        }
        switch (resolveOperator(name, *target, dummy.get(), pos)) {
        case OperatorChoice::Member: {
            std::vector<ExprPtr> args;
            if (!prefix) args.push_back(std::move(dummy));
            return memberCallWith(std::move(target), self, name, pos,
                                  std::move(args));
        }
        case OperatorChoice::NonMember: {
            std::vector<ExprPtr> args;
            args.push_back(std::move(target));
            if (!prefix) args.push_back(std::move(dummy));
            const Signature &sig = resolveOverload(name, args, pos);
            applyDefaults(sig, args, pos);
            return completeCall(name, sig.symbol, nullptr, sig.returns,
                                sig.params, sig.variadic, pos, std::move(args),
                                !sig.owner.empty());
        }
        case OperatorChoice::None:
            src_.fail(pos, std::string("'") + spelling + "' needs a '" + name +
                           "' for '" + self->describe() + "', and there is none");
        }
    }

    if (prefix) {
        ExprPtr one(new Num(1LL));
        one->setType(types_.intType());
        return compound(increment ? BinOp::Add : BinOp::Sub, std::move(target),
                        std::move(one), pos);
    }

    const char *what = increment ? "the operand of postfix '++'"
                                 : "the operand of postfix '--'";
    requireAssignable(*target, pos, what);
    const Type *t = target->type();
    // std::nullptr_t is a scalar and is still not something to step: it has
    // one value, so there is no next one.
    if (!t->isScalar() || t->isNullPtr())
        src_.fail(pos, std::string(what) + " needs a number or a pointer, not '" +
                       t->describe() + "'");
    if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(target.get()))
        if (m->isBitField())
            src_.fail(pos, "postfix '++' and '--' on a bit-field are not supported "
                           "yet - the prefix form works, and so does 'f.a = f.a + 1'");
    if (t->isPointer() && !t->pointee()->isComplete())
        src_.fail(pos, std::string(what) + " is '" + t->describe() +
                       "', and there is no size to step by");

    long long step = t->isPointer() ? t->pointee()->size(target_) : 1;
    ExprPtr n(new Postfix(std::move(target), increment, step));
    n->setType(t);
    return n;
}

ExprPtr Parser::conditional() {
    ExprPtr cond = logicalOr();
    if (!peek().is("?")) return cond;

    std::size_t pos = peek().pos;
    at_++;
    cond = decay(std::move(cond));
    cond = contextualScalar(std::move(cond), pos, "the condition of '?:'");

    // **What each arm builds, only that arm may destroy.** The entries each
    // one adds are noted so the class path below can guard them.
    const std::size_t tempsBeforeA = pendingTemps_.size();
    ExprPtr a = decay(expr());
    const std::size_t tempsAfterA = pendingTemps_.size();
    expect(":");
    ExprPtr b = decay(conditional());
    const std::size_t tempsAfterB = pendingTemps_.size();

    const Type *ta = a->type();
    const Type *tb = b->type();
    const Type *result = nullptr;

    // **[expr.cond]/4: both arms glvalues of one type, and the `?:` is one
    // too.**
    if (ta == tb && isLvalue(*a) && isLvalue(*b)) {
        const Type *ptr = types_.pointerTo(ta);
        ExprPtr at(new Unary('&', std::move(a)));
        at->setType(ptr);
        ExprPtr bt(new Unary('&', std::move(b)));
        bt->setType(ptr);
        ExprPtr which(new Conditional(std::move(cond), std::move(at),
                                      std::move(bt)));
        which->setType(ptr);
        ExprPtr n(new Unary('*', std::move(which)));
        n->setType(ta);
        return n;
    }

    if (ta->isArithmetic() && tb->isArithmetic()) {
        result = usualArithmetic(ta, tb);
        a = convert(std::move(a), result);
        b = convert(std::move(b), result);
    } else if (ta->unqualified()->isStructOrUnion() ||
               tb->unqualified()->isStructOrUnion()) {
        // **[expr.cond]/3: each arm is tried as the target for the other**, and
        // exactly one must convert - asked before anything is built, because
        // both directions have to be known before either is taken.
        const Type *pa = ta->unqualified();
        const Type *pb = tb->unqualified();
        const Type *want = nullptr;
        if (pa == pb) {
            // One class, two qualifications: the result is a prvalue, so
            // neither arm's constness reaches it.
            want = pa;
        } else {
            const bool aToB = pb->isStructOrUnion() &&
                              convertingConstructor(pb, *a) != nullptr;
            const bool bToA = pa->isStructOrUnion() &&
                              convertingConstructor(pa, *b) != nullptr;
            if (aToB && !bToA) want = pb;
            else if (bToA && !aToB) want = pa;
            else
                src_.fail(pos, std::string("the arms of '?:' are '") +
                               ta->describe() + "' and '" + tb->describe() +
                               (aToB ? "', and each converts to the other, so "
                                       "neither is the answer"
                                     : "', and neither converts to the other"));
        }

        // **The answer needs storage of its own**, which is the whole of why
        // this was refused: a `Conditional` yields a value the backends move
        // as a scalar and a class has nowhere to be moved to.
        const int slot = allocateFrameSlot(want);
        int guard = 0;
        if (destructorOf(want) != nullptr) {
            guard = guardFlag();
            pendingTemps_.push_back(Temporary{ slot, want, guard });
        }
        ExprPtr armA = buildInto(want, slot, std::move(a), guard, pos);
        armA = markArmTemporaries(std::move(armA), tempsBeforeA, tempsAfterA);
        ExprPtr armB = buildInto(want, slot, std::move(b), guard, pos);
        armB = markArmTemporaries(std::move(armB), tempsAfterA, tempsAfterB);
        ExprPtr chosen(new Conditional(std::move(cond), std::move(armA),
                                       std::move(armB)));
        chosen->setType(types_.intType());

        ExprPtr again(Var::local("$cond", slot));
        again->setType(want);
        ExprPtr at(new Unary('&', std::move(again)));
        at->setType(types_.pointerTo(want));
        ExprPtr both(new Comma(std::move(chosen), std::move(at)));
        both->setType(types_.pointerTo(want));
        ExprPtr made(new Unary('*', std::move(both)));
        made->setType(want);
        // About to expire, like any other temporary: without the mark, passing
        // one by value would reach for the copy constructor.
        made->setXvalue();
        return made;
    } else if (ta == tb) {
        result = ta;
    } else if (ta->isPointer() && isNullConstant(*b)) {
        result = ta;
        b = convert(std::move(b), result);
    } else if (tb->isPointer() && isNullConstant(*a)) {
        result = tb;
        a = convert(std::move(a), result);
    } else {
        src_.fail(pos, "the arms of '?:' have incompatible types '" +
                       ta->describe() + "' and '" + tb->describe() + "'");
    }

    ExprPtr n(new Conditional(std::move(cond), std::move(a), std::move(b)));
    n->setType(result);
    return n;
}

ExprPtr Parser::assign() {
    ExprPtr n = conditional();

    static const struct { const char *tok; BinOp op; } kCompound[] = {
        { "+=", BinOp::Add }, { "-=", BinOp::Sub }, { "*=", BinOp::Mul },
        { "/=", BinOp::Div }, { "%=", BinOp::Mod }, { "&=", BinOp::BitAnd },
        { "|=", BinOp::BitOr }, { "^=", BinOp::BitXor },
        { "<<=", BinOp::Shl }, { ">>=", BinOp::Shr },
    };
    for (const auto &c : kCompound) {
        if (peek().is(c.tok)) {
            std::size_t pos = peek().pos; at_++;
            return compound(c.op, std::move(n), decay(assign()), pos);
        }
    }

    if (!peek().is("=")) return n;
    std::size_t pos = peek().pos;
    at_++;

    requireAssignable(*n, pos, "the left of '='");

    const Type *to = n->type();
    ExprPtr value = decay(assign());

    // **`t = 5` where the class declares an `operator=` that takes an int.**
    if (to->unqualified()->isStructOrUnion() && value->type() != nullptr &&
        value->type()->unqualified() != to->unqualified() &&
        resolveOperator("operator=", *n, value.get(), pos) ==
            OperatorChoice::Member) {
        std::vector<ExprPtr> args;
        args.push_back(std::move(value));
        const Type *objectType = n->type();
        return memberCallWith(std::move(n), objectType, "operator=", pos,
                              std::move(args));
    }

    checkAssignable(*value, to, pos, "the left of '='");

    // **A class with a copy assignment of its own is assigned by calling it**,
    // not by moving its bytes - the one for the value's category, an lvalue
    // taking `const T &` and an xvalue or prvalue `T &&` (review A22).
    if (const Signature *op = copyAssignOf(to->unqualified())) {
        const std::vector<std::size_t> *set = overloadsOf(assignmentKey(to->unqualified()->tag()));
        if (set != nullptr) {
            const bool wantMove = !isLvalue(*value);
            for (std::size_t i = 0; i < set->size(); i++) {
                const Signature &cand = functions_[(*set)[i]];
                if (cand.params.empty()) continue;
                if (cand.params[0]->isReference() && cand.params[0]->isRValueReference() == wantMove) {
                    op = &cand;
                    break;
                }
            }
        }
        markUsed(op);
        const Type *selfPtr = types_.pointerTo(to->unqualified());
        ExprPtr addr(new Unary('&', std::move(n)));
        addr->setType(selfPtr);
        std::vector<ExprPtr> args;
        args.push_back(std::move(addr));
        args.push_back(std::move(value));
        std::vector<const Type *> ps;
        ps.push_back(selfPtr);
        ps.push_back(op->params[0]);
        ExprPtr call = completeCall(to->unqualified()->tag() + "::operator=",
                                    op->symbol, nullptr, selfPtr, ps, false, pos,
                                    std::move(args));
        // It answers `X &`, which is a pointer below the parser - so what the
        // expression yields is that pointer read back, and `(a = b).m` goes on
        // working the way it does for a written assignment.
        ExprPtr result(new Unary('*', std::move(call)));
        result->setType(to->unqualified());
        return result;
    }

    // **A class that declares a move constructor has no bytewise assignment to
    // fall back on**: [class.copy]/23 deletes the implicit copy assignment, and
    // the deletion is unconditional. An implicit move constructor deletes nothing.
    if (to->unqualified()->isStructOrUnion()) {
        const Signature *mv = moveConstructorOf(to->unqualified());
        if (mv != nullptr && !mv->implicit)
            src_.fail(pos, "'" + to->unqualified()->describe() + "' declares "
                           "a move constructor, so its copy assignment is "
                           "deleted and '=' cannot copy one - assign the "
                           "members themselves, or give the class a copy "
                           "assignment operator");
    }
    ExprPtr node(new Assign(std::move(n), convert(std::move(value), to)));
    node->setType(to);
    return node;
}

ExprPtr Parser::expr() {
    ExprPtr n = assign();
    while (consume(",")) {
        ExprPtr right = decay(assign());
        const Type *t = right->type();
        ExprPtr c(new Comma(std::move(n), std::move(right)));
        c->setType(t);
        n = std::move(c);
    }
    return n;
}

