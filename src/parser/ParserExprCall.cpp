// The parser: calls, and what a call has to do to its arguments. Reading an
// argument list, filling in defaults, copying a by-value class parameter, the
// full expression's temporaries, and the member calls that lower to all of that.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>


void Parser::parseArguments(std::vector<ExprPtr> &args) {
    if (consume(")")) return;
    for (;;) {
        // **`rest...` - one thing written, one argument per member.** The names
        // were made when the parameter list expanded, so this is a lookup and not
        // a substitution: whatever `rest$0` and `rest$1` are now is what goes.
        if (peek().kind == TokenKind::Ident && peekAt(1).is("...")) {
            auto pk = packs_.find(peek().text);
            if (pk != packs_.end() && !pk->second.names.empty()) {
                at_ += 2;
                for (std::size_t i = 0; i < pk->second.names.size(); i++) {
                    ExprPtr one = objectRef(pk->second.names[i]);
                    if (one == nullptr)
                        src_.fail(peek().pos, "'" + pk->second.names[i] +
                                              "' went missing from this pack");
                    args.push_back(decay(std::move(one)));
                }
                if (consume(")")) break;
                expect(",");
                continue;
            }
            // An empty pack expands to no arguments at all.
            if (pk != packs_.end() && pk->second.types.empty()) {
                at_ += 2;
                if (consume(")")) break;
                expect(",");
                continue;
            }
        }
        args.push_back(assign());
        if (consume(")")) break;
        expect(",");
    }
}

// **Split from completeCall so that overload resolution can stand between them.**
// Choosing a function needs the arguments and converting them needs the function,
// so the two cannot be one pass. A call through a pointer still comes here.
ExprPtr Parser::finishCall(const std::string &name, const std::string &symbol,
                           ExprPtr callee, const Type *returns,
                           const std::vector<const Type *> &params,
                           bool variadic, std::size_t pos) {
    std::vector<ExprPtr> args;
    parseArguments(args);
    return completeCall(name, symbol, std::move(callee), returns, params,
                        variadic, pos, std::move(args));
}

// The caller's half of passing a class by value - a temporary, the copy run into
// it, its address handed over, all one expression - and below it the end of a full
// expression, where those temporaries are destroyed in the reverse of their order.
void Parser::flushTemporaries(std::vector<StmtPtr> &into) {
    if (pendingTemps_.empty()) return;
    std::vector<Temporary> mine;
    mine.swap(pendingTemps_);
    for (std::size_t k = mine.size(); k-- > 0; ) {
        const Signature *dtor = destructorOf(mine[k].type);
        if (dtor == nullptr) continue;
        ExprPtr what(Var::local("$copy", mine[k].slot));
        what->setType(mine[k].type);
        ExprPtr at(new Unary('&', std::move(what)));
        at->setType(types_.pointerTo(mine[k].type));
        StmtPtr call(new ExprStmt(destructorCall(std::move(at), *dtor, 0)));
        // **Under its guard, and cleared with it.**
        if (mine[k].flag != 0) {
            ExprPtr live(Var::local("$guard", mine[k].flag));
            live->setType(types_.intType());
            std::vector<StmtPtr> both;
            both.push_back(std::move(call));
            both.push_back(StmtPtr(new ExprStmt(setGuard(mine[k].flag, 0))));
            call.reset(new If(std::move(live),
                              StmtPtr(new Block(std::move(both))), StmtPtr()));
        }
        into.push_back(std::move(call));
    }
}

// **Take one temporary off the pending list**, because something else has
// become responsible for it.
int Parser::guardFlag() {
    const int slot = allocateFrameSlot(types_.intType());
    guardSlots_.push_back(slot);
    return slot;
}

ExprPtr Parser::setGuard(int flag, int value) {
    ExprPtr f(Var::local("$guard", flag));
    f->setType(types_.intType());
    ExprPtr n(new Num(static_cast<long long>(value)));
    n->setType(types_.intType());
    ExprPtr set(new Assign(std::move(f), std::move(n)));
    set->setType(types_.intType());
    return set;
}

bool Parser::releaseTemporary(const Expr &value) {
    const Expr *at = &value;
    for (;;) {
        if (const Comma *c = dynamic_cast<const Comma *>(at)) { at = &c->right(); continue; }
        // Both, and in either order: classTemporary hands back
        // `*(ctor(&tmp), &tmp)`, so the walk passes a dereference, a comma and
        // an address-of before it reaches the slot.
        if (const Unary *u = dynamic_cast<const Unary *>(at))
            if (u->op() == '*' || u->op() == '&') { at = &u->operand(); continue; }
        break;
    }
    // **A call's result is a temporary too, and it is not a `Var`.**
    int slot = -1;
    if (const Var *v = dynamic_cast<const Var *>(at)) {
        if (!v->isLocal()) return false;
        slot = v->offset();
    } else if (const Call *c = dynamic_cast<const Call *>(at)) {
        if (c->resultSlot() == 0) return false;
        slot = c->resultSlot();
    } else {
        return false;
    }
    for (std::size_t i = 0; i < pendingTemps_.size(); i++)
        if (pendingTemps_[i].slot == slot) {
            pendingTemps_.erase(pendingTemps_.begin() + i);
            return true;
        }
    return false;
}

// **[class.temporary]/5: a temporary bound to a reference lives as long as the
// reference does**, not to the end of the full expression that made it.
bool Parser::extendTemporary(const Expr &addr, const std::string &name) {
    const Expr *at = &addr;
    for (;;) {
        if (const Comma *c = dynamic_cast<const Comma *>(at)) { at = &c->right(); continue; }
        if (const Unary *u = dynamic_cast<const Unary *>(at))
            if (u->op() == '*' || u->op() == '&') { at = &u->operand(); continue; }
        break;
    }

    int slot = 0;
    if (const Var *v = dynamic_cast<const Var *>(at)) {
        if (!v->isLocal()) return false;
        slot = v->offset();
    } else if (const Call *c = dynamic_cast<const Call *>(at)) {
        if (c->resultSlot() == 0) return false;
        slot = c->resultSlot();
    } else {
        return false;
    }

    for (std::size_t i = 0; i < pendingTemps_.size(); i++)
        if (pendingTemps_[i].slot == slot) {
            const Type *cls = pendingTemps_[i].type;
            pendingTemps_.erase(pendingTemps_.begin() + i);
            // The guard flag goes with the entry.
            if (destructorOf(cls) != nullptr)
                alive_.push_back(Alive{ name, slot, cls });
            return true;
        }
    return false;
}

ExprPtr Parser::endFullExpression(ExprPtr e) {
    if (pendingTemps_.empty()) return e;
    std::vector<Temporary> mine;
    mine.swap(pendingTemps_);

    // **Every guard starts clear, in front of the whole expression.** A
    // statement inside a loop runs again, and a flag left set from the turn
    // before would have the pad destroy an object this turn never built.
    for (std::size_t k = 0; k < mine.size(); k++) {
        if (mine[k].flag == 0) continue;
        const Type *et = e->type();
        ExprPtr seq(new Comma(setGuard(mine[k].flag, 0), std::move(e)));
        seq->setType(et);
        e = std::move(seq);
    }

    const Type *t = e->type();
    const bool hasValue = t != nullptr && !t->isVoid() && !t->isFunction();
    int keep = 0;
    if (hasValue) {
        keep = allocateFrameSlot(t);
        ExprPtr where(Var::local("$full", keep));
        where->setType(t);
        ExprPtr save(new Assign(std::move(where), std::move(e)));
        save->setType(t);
        e = std::move(save);
    }
    for (std::size_t k = mine.size(); k-- > 0; ) {
        const Signature *dtor = destructorOf(mine[k].type);
        if (dtor == nullptr) continue;
        ExprPtr what(Var::local("$copy", mine[k].slot));
        what->setType(mine[k].type);
        ExprPtr at(new Unary('&', std::move(what)));
        at->setType(types_.pointerTo(mine[k].type));
        ExprPtr gone = destructorCall(std::move(at), *dtor, 0);
        // Clear the guard with the destruction, so that a pad later in the
        // same block does not destroy what this statement already has - and
        // ask it first, for the same reason `flushTemporaries` does.
        if (mine[k].flag != 0) {
            ExprPtr clear(new Comma(std::move(gone), setGuard(mine[k].flag, 0)));
            clear->setType(types_.intType());
            ExprPtr live(Var::local("$guard", mine[k].flag));
            live->setType(types_.intType());
            ExprPtr none(new Num(0LL));
            none->setType(types_.intType());
            ExprPtr asked(new Conditional(std::move(live), std::move(clear),
                                          std::move(none)));
            asked->setType(types_.intType());
            gone = std::move(asked);
        }
        ExprPtr seq(new Comma(std::move(e), std::move(gone)));
        seq->setType(t);
        e = std::move(seq);
    }
    if (hasValue) {
        ExprPtr back(Var::local("$full", keep));
        back->setType(t);
        ExprPtr seq(new Comma(std::move(e), std::move(back)));
        seq->setType(t);
        e = std::move(seq);
    }
    // What this statement made, for whoever opens the cleanup regions.
    for (std::size_t k = 0; k < mine.size(); k++)
        if (mine[k].flag != 0) statementTemps_.push_back(mine[k]);
    return e;
}

ExprPtr Parser::materialiseCopy(const Type *type, ExprPtr arg, std::size_t pos,
                                const std::string &what,
                                std::vector<Temporary> &destroy) {
    const Type *cls = type->unqualified();
    // **A by-value parameter is initialised from the argument, so anything that is
    // not an lvalue moves into it.** This path predates rvalue references and
    // reaches for the copy by name, so the choice is made here instead.
    const Signature *cc = nullptr;
    if (!isLvalue(*arg)) cc = moveConstructorOf(cls);
    if (cc == nullptr) cc = copyConstructorOf(cls);

    // **[dcl.init]/17 makes this copy-initialization, so the constructor it picks
    // may not be `explicit`** - the third place that rule bites and the least
    // obvious, nothing at the call site being written with an `=` in it.
    if (cc != nullptr && cc->isExplicit)
        src_.fail(pos, what + " is a '" + cls->describe() + "' passed by "
                       "value, which copy-initialises it from the argument - "
                       "and that may not pick the 'explicit' constructor this "
                       "class copies with. Take it by reference, or make the "
                       "copy constructor not explicit");

    // **A class that declares a move constructor cannot be passed by value from an
    // lvalue**: [class.copy]/7 deletes its implicit copy constructor, and here that
    // deletion is an absence, which the byte path below would answer for.
    if (cc == nullptr) {
        const Signature *mv = moveConstructorOf(cls);
        if (mv != nullptr && !mv->implicit)
            src_.fail(pos, what + " is a '" + cls->describe() + "' passed by "
                           "value, and this class declares a move constructor "
                           "- so its copy constructor is deleted and an lvalue "
                           "cannot be copied into the parameter. Write "
                           "'static_cast<" + cls->describe() + " &&>(...)' to "
                           "move out of it, or give the class a copy "
                           "constructor");
    }

    // **A class that only has a destructor still goes by address on Itanium**, and
    // the caller's copy is a move of bytes rather than a call, copying it being
    // trivial. What is not trivial is destroying it, which is why it travels so.
    if (cc == nullptr) {
        checkAssignable(*arg, cls, pos, what);
        const int plain = allocateFrameSlot(cls);
        const Type *to = types_.pointerTo(cls);
        int guard = 0;
        if (destructorOf(cls) != nullptr) {
            guard = guardFlag();
            destroy.push_back(Temporary{ plain, cls, guard });
        }

        ExprPtr slot(Var::local("$copy", plain));
        slot->setType(cls);
        ExprPtr store(new Assign(std::move(slot), std::move(arg)));
        store->setType(cls);
        // The copy exists from here, and the pad may run from here on.
        if (guard != 0) {
            ExprPtr mark(new Comma(std::move(store), setGuard(guard, 1)));
            mark->setType(types_.intType());
            store = std::move(mark);
        }

        ExprPtr again(Var::local("$copy", plain));
        again->setType(cls);
        ExprPtr at(new Unary('&', std::move(again)));
        at->setType(to);

        ExprPtr node(new Comma(std::move(store), std::move(at)));
        node->setType(to);
        return node;
    }
    if (cc->access != Access::Public && !insideAccessOf(cls, cc->access) &&
        !isFriendOf(cls))
        src_.fail(pos, "'" + cls->describe() + "' is passed by value as " + what +
                       ", which copies it, and its copy constructor is " +
                       (cc->access == Access::Private ? "private" : "protected"));
    checkAssignable(*arg, cls, pos, what);

    const int tmp = allocateFrameSlot(cls);
    const Type *ptr = types_.pointerTo(cls);
    int guard = 0;
    if (destructorOf(cls) != nullptr) {
        guard = guardFlag();
        destroy.push_back(Temporary{ tmp, cls, guard });
    }

    // **Elision, where the argument is already one of these coming back through a
    // hidden pointer.** The call builds its result straight into the temporary this
    // argument needs, so no copy constructor runs - as clang does at -O0.
    if (Call *made = dynamic_cast<Call *>(arg.get())) {
        if (made->type() == cls && returnsIndirectly(cls, made->hasThis())) {
            claimCallResult(*made, tmp);
            ExprPtr built(Var::local("$copy", tmp));
            built->setType(cls);
            ExprPtr at(new Unary('&', std::move(built)));
            at->setType(ptr);
            if (guard != 0) {
                ExprPtr mark(new Comma(std::move(arg), setGuard(guard, 1)));
                mark->setType(types_.intType());
                arg = std::move(mark);
            }
            ExprPtr node(new Comma(std::move(arg), std::move(at)));
            node->setType(ptr);
            return node;
        }
    }

    markUsed(cc);

    ExprPtr slot(Var::local("$copy", tmp));
    slot->setType(cls);
    ExprPtr addr(new Unary('&', std::move(slot)));
    addr->setType(ptr);

    std::vector<ExprPtr> ctorArgs;
    ctorArgs.push_back(std::move(addr));
    ctorArgs.push_back(std::move(arg));
    std::vector<const Type *> ps;
    ps.push_back(ptr);
    ps.push_back(cc->params[0]);
    ExprPtr build = completeCall(cls->tag(), cc->symbol, nullptr,
                                 types_.get(Kind::Void), ps, false, pos,
                                 std::move(ctorArgs));

    if (guard != 0) {
        ExprPtr mark(new Comma(std::move(build), setGuard(guard, 1)));
        mark->setType(types_.intType());
        build = std::move(mark);
    }

    ExprPtr again(Var::local("$copy", tmp));
    again->setType(cls);
    ExprPtr result(new Unary('&', std::move(again)));
    result->setType(ptr);

    ExprPtr node(new Comma(std::move(build), std::move(result)));
    node->setType(ptr);
    return node;
}

ExprPtr Parser::completeCall(const std::string &name, const std::string &symbol,
                             ExprPtr callee, const Type *returns,
                             const std::vector<const Type *> &params,
                             bool variadic, std::size_t pos,
                             std::vector<ExprPtr> args, bool hasThis,
                             int vbInit) {
    // **cl's hidden most-derived flag, added here and nowhere else.**
    if (vbInit >= 0 && !msVbaseCtors_.empty() &&
        msVbaseCtors_.count(symbol) != 0) {
        std::vector<const Type *> withFlag(params);
        withFlag.push_back(types_.intType());
        ExprPtr flag(new Num(static_cast<long long>(vbInit)));
        flag->setType(types_.intType());
        args.push_back(std::move(flag));
        return completeCall(name, symbol, std::move(callee), returns, withFlag,
                            variadic, pos, std::move(args), hasThis, -1);
    }
    if (variadic ? args.size() < params.size() : args.size() != params.size())
        src_.fail(pos, "'" + name + "' takes " + (variadic ? "at least " : "") +
                       std::to_string(params.size()) + " argument(s), given " +
                       std::to_string(args.size()));

    // **A call through a pointer promises nothing.**
    if (callee != nullptr) mayThrow_++;

    // Temporaries this call makes for its by-value class arguments, and which this call therefore has to destroy once it returns.
    std::vector<Temporary> destroy;

    for (std::size_t i = 0; i < args.size(); i++) {
        if (i >= params.size()) {
            args[i] = defaultPromote(decay(std::move(args[i])));
            continue;
        }
        std::string what = "argument " + std::to_string(i + 1) + " of '" + name + "'";
        // **[over.ics.user]: a converting constructor may be called to make an
        // argument.**
        if (ExprPtr made = userConversion(params[i], args[i], pos))
            args[i] = std::move(made);
        if (params[i]->isReference()) {
            args[i] = bindReference(params[i], std::move(args[i]), pos, what);
            continue;
        }
        // **A class whose copy is a constructor call is copied by the caller**,
        // into a temporary the caller owns, and the callee receives its address.
        // Measured on all three targets: clang and cl copy at the call site.
        if (passedByAddress(params[i])) {
            args[i] = materialiseCopy(params[i], std::move(args[i]), pos, what,
                                      destroy);
            continue;
        }
        args[i] = decay(std::move(args[i]));
        checkAssignable(*args[i], params[i], pos, what);
        args[i] = convert(std::move(args[i]), params[i]);
    }

    int slot = returns->isStructOrUnion() ? allocateFrameSlot(returns) : 0;
    int named = static_cast<int>(params.size());

    std::vector<int> argSlots(args.size(), 0);
    for (std::size_t i = 0; i < args.size(); i++)
        if (args[i]->type()->isStructOrUnion())
            argSlots[i] = allocateFrameSlot(args[i]->type());

    Call *call = new Call(name, std::move(callee), std::move(args), variadic,
                          slot, named, std::move(argSlots));
    call->setSymbol(symbol);
    call->setHasThis(hasThis);
    ExprPtr n(call);
    n->setType(returns);

    // **The caller destroys the copies it made** - measured from clang. The
    // Microsoft ABI puts that on the callee; docs/CONFORMANCE.md has the cost.
    // They go to the full expression, which is when the standard destroys them.
    if (!target_.microsoftNames())
        for (std::size_t k = 0; k < destroy.size(); k++)
            pendingTemps_.push_back(destroy[k]);

    // **And the object the call returns is a temporary like any other.**
    if (slot != 0 && destructorOf(returns) != nullptr)
        pendingTemps_.push_back(Temporary{ slot, returns->unqualified(), 0 });

    // A call that returns a reference is a glvalue - useReference wraps the
    // returned address in the dereference the caller named - an lvalue for
    // `T &` and an xvalue for `T &&`, [expr.call]/10: what std::move binds.
    const bool xvalue = n->type()->isReference() && n->type()->isRValueReference();
    ExprPtr made = useReference(std::move(n));
    if (xvalue) made->setXvalue();
    return made;
}

void Parser::claimCallResult(Call &c, int slot) {
    const int had = c.resultSlot();
    if (had != 0)
        for (std::size_t i = 0; i < pendingTemps_.size(); i++)
            if (pendingTemps_[i].slot == had) {
                pendingTemps_.erase(pendingTemps_.begin() +
                                    static_cast<long>(i));
                break;
            }
    c.setResultSlot(slot);
}

// A call through an object: `p.move(1, 2)`. The object's address goes in front of
// the written arguments and the declared parameters gain a matching leading
// pointer, so from here down it is an ordinary call.
const Type *Parser::findMemberOwner(const Type *cls,
                                    const std::string &name) const {
    if (cls == nullptr) return nullptr;
    const Type *c = cls->unqualified();
    if (overloadsOf(c->tag() + "::" + name) != nullptr) return c;
    const std::vector<Type::BaseSpec> &bases = c->bases();
    for (std::size_t i = 0; i < bases.size(); i++)
        if (const Type *found = findMemberOwner(bases[i].type, name)) return found;
    return nullptr;
}

ExprPtr Parser::memberCall(ExprPtr object, const Type *cls,
                           const std::string &name, std::size_t pos) {
    std::vector<ExprPtr> args;
    parseArguments(args);
    return memberCallWith(std::move(object), cls, name, pos, std::move(args));
}

// The same call with its arguments already in hand. **An overloaded operator is
// what split this in two**: `a + b` has parsed its right operand long before it
// knows there is a call here, so the arguments cannot come off the token stream.
ExprPtr Parser::memberCallWith(ExprPtr object, const Type *cls,
                               const std::string &name, std::size_t pos,
                               std::vector<ExprPtr> args,
                               const Type *forceOwner) {
    const Type *plain = cls->unqualified();

    // **A member function is looked for up the base chain**, unlike a data
    // member, which the layout copied down: a member lives at an offset and a
    // function under a name.
    const Type *owner = forceOwner != nullptr ? forceOwner->unqualified()
                                              : findMemberOwner(plain, name);
    if (owner == nullptr) owner = plain;
    std::string key = owner->tag() + "::" + name;

    const Signature &sig = resolveOverload(key, args, pos, cls);
    applyDefaults(sig, args, pos);

    // Now there IS an inside, and this is where it starts to mean something:
    // a private member is reachable from another member of the same class.
    if (sig.access != Access::Public && !insideAccessOf(plain, sig.access) &&
        !insideAccessOf(owner, sig.access) && !isFriendOf(plain) &&
        !isFriendOf(owner)) {
        const char *how = sig.access == Access::Private ? "private" : "protected";
        src_.fail(pos, "'" + name + "' is " + how + " in '" + plain->describe() +
                       "' - it can be called only from inside the class");
    }
    // **A static member called through an object.**
    if (sig.isStaticMember) {
        ExprPtr call = completeCall(key, sig.symbol, nullptr, sig.returns,
                                    sig.params, sig.variadic, pos,
                                    std::move(args), false);
        if (clonePure(*object) != nullptr) return call;
        const Type *rt = call->type();
        ExprPtr both(new Comma(std::move(object), std::move(call)));
        both->setType(rt);
        return both;
    }

    if (cls->isConst() && !sig.constThis)
        src_.fail(pos, "'" + name + "' is not a const member function, and this "
                       "object is const - calling it could change what the "
                       "const promised not to");

    // `this` inside the base's member function is a Base *, and the base
    // subobject sits at offset 0 - so the derived address IS that pointer and
    // the conversion costs nothing at run time.
    const Type *self = owner;
    const Type *pointee = sig.constThis ? types_.withConst(self) : self;
    const Type *thisType = types_.pointerTo(pointee);

    // **`this` is the base's address, not the object's**, and they differ once a
    // class has a second base: B sits at offset 4 in C, so B's member functions
    // expect &c + 4. convert() moves a pointer to a base, so it is built as one.
    ExprPtr addr(new Unary('&', std::move(object)));
    addr->setType(types_.pointerTo(plain));
    if (owner != plain) addr = convert(std::move(addr), thisType);
    else addr->setType(thisType);

    std::vector<const Type *> full;
    full.push_back(thisType);
    for (std::size_t i = 0; i < sig.params.size(); i++) full.push_back(sig.params[i]);

    // **A virtual call reads the slot rather than naming the function.** The
    // object's first word is the vptr and the slot is at the same index in every
    // class of the chain; below the load it is an ordinary indirect call.
    ExprPtr callee;
    ExprPtr keepAddress;
    // **A qualified call is never dispatched** - [expr.call]/1 - which is the
    // whole reason an override writes `Base::f(...)` to reach what it replaced.
    if (sig.isVirtual && forceOwner == nullptr) {
        int index = -1;
        const std::vector<VSlot> &slots = vtables_[plain->tag()];
        for (std::size_t i = 0; i < slots.size(); i++) {
            if (overrides(slots[i], name, sig.params, sig.constThis)) {
                index = static_cast<int>(i);
                break;
            }
        }
        // **A function of a base off the primary chain** - a second base, a
        // virtual one - is not in this class's table: `addr` is that base's
        // subobject by now, and its own vptr and slot are what dispatch it.
        if (index < 0 && owner != plain) {
            const std::vector<VSlot> &theirs = vtables_[owner->tag()];
            for (std::size_t i = 0; i < theirs.size(); i++) {
                if (overrides(theirs[i], name, sig.params, sig.constThis)) {
                    index = static_cast<int>(i);
                    break;
                }
            }
        }
        if (index < 0)
            src_.fail(pos, "'" + name + "' is virtual but has no vtable slot in "
                           "'" + plain->describe() + "'");

        const Type *fnType = types_.functionType(sig.returns, full, sig.variadic);
        const Type *fnPtr = types_.pointerTo(fnType);
        const Type *table = types_.pointerTo(fnPtr);       // what the vptr is

        // **The address is needed twice** - to read the vptr out of the object and
        // as the `this` argument - and an expression is used up when it is moved.
        // So it goes into a slot and both readers name that, as `new` does.
        int slot = allocateFrameSlot(thisType);
        std::string temp = ".vc" + std::to_string(refTemps_++);

        ExprPtr held(Var::local(temp, slot));
        held->setType(thisType);
        keepAddress.reset(new Assign(std::move(held), std::move(addr)));
        keepAddress->setType(thisType);

        ExprPtr again(Var::local(temp, slot));
        again->setType(thisType);
        addr.reset(Var::local(temp, slot));
        addr->setType(thisType);

        ExprPtr forLoad(new Cast(types_.pointerTo(table), std::move(again)));
        forLoad->setType(types_.pointerTo(table));
        ExprPtr vptr(new Unary('*', std::move(forLoad)));
        vptr->setType(table);

        if (index != 0) {
            // Bytes again, and for the same reason as the header skip in the
            // constructor: a hand-built Add is not scaled by the pointee.
            ExprPtr at(new Num(static_cast<long long>(index) * fnPtr->size(target_)));
            at->setType(types_.intType());
            ExprPtr moved(new Binary(BinOp::Add, std::move(vptr), std::move(at)));
            moved->setType(table);
            vptr = std::move(moved);
        }
        ExprPtr entry(new Unary('*', std::move(vptr)));
        entry->setType(fnPtr);
        callee = std::move(entry);
    }

    std::vector<ExprPtr> all;
    all.push_back(std::move(addr));
    for (std::size_t i = 0; i < args.size(); i++) all.push_back(std::move(args[i]));

    // The object's address went in front of the written arguments a few lines up,
    // which is what makes this an ordinary call from here down - and what the
    // Microsoft ABI has to be told, its hidden return pointer following `this`.
    ExprPtr call = completeCall(name, sig.symbol, std::move(callee), sig.returns,
                                full, sig.variadic, pos, std::move(all), true);
    if (keepAddress == nullptr) return call;

    // The address is saved, then the call reads it - in that order, which the
    // comma operator is exactly for.
    const Type *result = call->type();
    ExprPtr both(new Comma(std::move(keepAddress), std::move(call)));
    both->setType(result);
    return both;
}

// [class.access]: a member that is not public may be named only from inside the
// class - and [class.friend], by a function the class granted access to. Those two
// are the only ways past a private member, and are asked in the same breath.
bool Parser::isFriendOf(const Type *cls) const {
    if (cls == nullptr || currentFunction_.empty()) return false;
    std::map<std::string, std::vector<std::string> >::const_iterator it =
        friends_.find(cls->unqualified()->tag());
    if (it == friends_.end()) return false;
    for (std::size_t i = 0; i < it->second.size(); i++)
        if (it->second[i] == currentFunction_) return true;
    return false;
}

// **A lambda has the access of the function it was written in** -
// [expr.prim.lambda]/7.
static bool derivesFrom(const Type *cls, const Type *want) {
    if (cls == nullptr) return false;
    const std::vector<Type::BaseSpec> &bs = cls->unqualified()->bases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        if (bs[i].type->unqualified() == want) return true;
        if (derivesFrom(bs[i].type, want)) return true;
    }
    return false;
}

bool Parser::insideAccessOf(const Type *cls, Access access) const {
    if (cls == nullptr) return false;
    const Type *want = cls->unqualified();
    // The class's own body reaches everything it declares - [class.access]/1.
    for (std::size_t i = 0; i < classStack_.size(); i++)
        if (classStack_[i] == want) return true;
    if (currentClass_ == nullptr) return false;
    if (currentClass_ == want) return true;
    if (access == Access::Protected && derivesFrom(currentClass_, want))
        return true;
    std::map<std::string, const Type *>::const_iterator outer =
        closureOuter_.find(currentClass_->unqualified()->tag());
    return outer != closureOuter_.end() &&
           outer->second->unqualified() == want;
}

bool Parser::accessibleFrom(const Type *from, const Type *owner,
                            Access a) const {
    if (a == Access::Public) return true;
    if (from == nullptr || owner == nullptr) return false;
    const Type *want = owner->unqualified();
    if (from->unqualified() == want) return true;
    return a == Access::Protected && derivesFrom(from->unqualified(), want);
}

void Parser::checkAccessible(const Type *object, const Member &m,
                             std::size_t pos) const {
    if (m.access == Access::Public) return;
    // **Asked about the class that declared it**, not the one it was reached
    // through: a base's members are copied down, so a private member of `B`
    // read through a `D` was asked about `D` and answered yes from inside `D`.
    const Type *owner = m.declaredIn != nullptr ? m.declaredIn : object;
    if (insideAccessOf(owner, m.access)) return;
    if (isFriendOf(owner)) return;
    // No fallback to the class it was reached *through*.
    const char *how = m.access == Access::Private ? "private" : "protected";
    // Named by the class that declared it: saying it is private in the derived
    // class sends the reader to a class whose source does not mention it.
    src_.fail(pos, "'" + m.name + "' is " + how + " in '" + owner->describe() +
                   "' - it can be named only from inside the class, and this "
                   "is outside it");
}
