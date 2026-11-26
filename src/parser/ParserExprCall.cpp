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
    OverloadTarget none(*this, nullptr);
    // `f(f(f(...)))` re-enters the whole expression grammar per call.
    const Nesting deep(this, peek().pos, "argument list");
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
        // **A braced list as an argument** is kept behind a placeholder until the parameter
        // it copy-list-initialises is known - [dcl.init]/16 cannot be applied before that.
        if (peek().is("{")) args.push_back(bracedPlaceholder(parseInitialiser()));
        else if (atBareFunctionArgument()) {
            OverloadTarget waits(*this, nullptr, true);
            args.push_back(assign());
        } else if (!expandPackPattern(args)) args.push_back(assign());
        if (consume(")")) break;
        expect(",");
    }
}

// The placeholder is a `void` constant nothing can rank, so a list that is never
// built fails resolution rather than reaching a backend.
ExprPtr Parser::bracedPlaceholder(Init in) {
    ExprPtr mark(new Num(0LL));
    mark->setType(types_.get(Kind::Void));
    bracedArgs_.push_back(BracedArg{ mark.get(), std::move(in) });
    return mark;
}

bool Parser::isBracedPlaceholder(const Expr &e) const {
    for (std::size_t i = 0; i < bracedArgs_.size(); i++)
        if (bracedArgs_[i].placeholder == &e) return true;
    return false;
}

// [over.ics.list] in the one shape built here: the parameter's own type decides what the
// braces make, and a reference parameter binds to the temporary so made.
ExprPtr Parser::bracedArgumentFor(const Type *param, Init &in, std::size_t pos) {
    const Type *to = param->isReference() ? param->referent() : param;
    // `T (&&)[N]` given `{1, 2, 3}` (CWG 1591): the array is a temporary of the call's full expression.
    if (param->isReference() && to->unqualified()->isArray()) return aggregateTemporary(to->unqualified(), in, pos);
    return listTemporaryFrom(to, in, true, pos);
}

const Parser::Init *Parser::bracedInitOf(const Expr &e) const {
    for (std::size_t i = 0; i < bracedArgs_.size(); i++)
        if (bracedArgs_[i].placeholder == &e) return &bracedArgs_[i].init;
    return nullptr;
}

// **[over.ics.list], as far as overload choice needs it**: whether the list
// could initialise this parameter at all, in listTemporaryFrom's order (a
// scalar, an initializer_list ctor, a non-explicit ctor, an aggregate).
bool Parser::bracedListViable(const Type *param, const Init &in) {
    if (param->isReference() && !param->isRValueReference() && !param->referent()->isConst())
        return false;  // the list makes a temporary, which binds no non-const lvalue reference
    const Type *plain = (param->isReference() ? param->referent() : param)->unqualified();
    const std::size_t n = in.items.size();
    if (plain->isVoid() || plain->isArray()) return false;
    if (!plain->isStructOrUnion()) return n == 0 || (n == 1 && !in.items[0].isList);
    if (isInitializerListType(plain) || plain->tag().empty()) return true;
    if (!hasConstructors(plain->tag()) || onlyImplicitConstructors(plain->tag())) return true;
    const Type *ilType = nullptr;
    if (n != 0 && initializerListConstructor(plain, &ilType) != nullptr) return true;
    // A constructor template's parameters are not known until it is deduced: it may take the list.
    if (n != 0 && memberTemplates_.count(constructorKey(plain->tag())) != 0) return true;
    const std::vector<std::size_t> *set = overloadsOf(constructorKey(plain->tag()));
    if (set == nullptr) return n == 0;
    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = functions_[(*set)[k]];
        if (f.isExplicit || n < leastArguments(f) || (!f.variadic && n > f.params.size())) continue;
        return true;
    }
    return false;
}

void Parser::buildBracedArguments(const std::vector<const Type *> &params,
                                  std::vector<ExprPtr> &args, std::size_t pos) {
    for (std::size_t i = 0; i < args.size(); i++) {
        const OverloadArg *oa = overloadArgOf(*args[i]);
        if (oa == nullptr) continue;
        ExprPtr picked = i < params.size() ? overloadArgumentFor(params[i], *oa) : nullptr;
        if (picked == nullptr)
            src_.fail(oa->pos, "'" + oa->key + "' names " + std::to_string(oa->set.size()) +
                               " functions, and none of them is of the type " +
                               (i < params.size() ? "'" + params[i]->describe() + "' that "
                                                  : std::string("no ")) +
                               "parameter " + std::to_string(i + 1) + " of this call takes");
        args[i] = std::move(picked);
    }
    for (std::size_t i = 0; i < args.size(); i++) {
        if (!isBracedPlaceholder(*args[i])) continue;
        if (i >= params.size())
            src_.fail(pos, "a braced list is given where this call has no parameter to take it");
        for (std::size_t k = 0; k < bracedArgs_.size(); k++)
            if (bracedArgs_[k].placeholder == args[i].get()) {
                Init in = std::move(bracedArgs_[k].init);
                bracedArgs_.erase(bracedArgs_.begin() + static_cast<long>(k));
                args[i] = bracedArgumentFor(params[i], in, pos);
                break;
            }
    }
}

// Before the set is ranked, every candidate is asked what it takes at a braced position:
// where they agree the list is built against that type, where they do not it is refused.
void Parser::buildBracedArguments(const std::string &key, std::vector<ExprPtr> &args,
                                  std::size_t pos, const Type *object) {
    pickOverloadArguments(key, args, object);
    bool any = false;
    for (std::size_t i = 0; i < args.size() && !any; i++) any = isBracedPlaceholder(*args[i]);
    if (!any) return;
    const std::string name = object != nullptr ? key : qualifyForLookup(key, &Parser::hasFunctionNamed);
    const std::vector<std::size_t> *set = overloadsOf(name);
    if (set == nullptr)
        src_.fail(pos, "a braced list is given as an argument of '" + key + "', and no function "
                       "of that name is declared to say what it should make");
    // **Only the candidates that could take this call are asked** (1.25): the number of arguments,
    // every written one convertible to its parameter, every braced one a list its parameter can be
    // initialised from (bracedListViable). If none is left, all of them are asked, as before 1.25.
    std::vector<std::size_t> asked;
    for (std::size_t k = 0; k < set->size(); k++) {
        const Signature &f = functions_[(*set)[k]];
        if (f.variadic ? args.size() < leastArguments(f)
                       : (args.size() > f.params.size() || args.size() < leastArguments(f))) continue;
        bool viable = true;
        for (std::size_t j = 0; j < args.size() && j < f.params.size() && viable; j++) {
            if (const Init *in = bracedInitOf(*args[j])) viable = bracedListViable(f.params[j], *in);
            else if (overloadArgOf(*args[j]) == nullptr) viable = rankArgument(*args[j], f.params[j]) != Rank::None;
        }
        if (viable) asked.push_back((*set)[k]);
    }
    if (asked.empty()) asked = *set;
    std::vector<const Type *> params;
    for (std::size_t i = 0; i < args.size(); i++) {
        const Type *agreed = nullptr;
        bool disagree = false;
        if (isBracedPlaceholder(*args[i]))
            for (std::size_t k = 0; k < asked.size(); k++) {
                const Signature &f = functions_[asked[k]];
                if (i >= f.params.size()) continue;
                const Type *p = f.params[i]->isReference() ? f.params[i]->referent()->unqualified()
                                                           : f.params[i]->unqualified();
                if (agreed == nullptr) agreed = f.params[i];
                else if (p != (agreed->isReference() ? agreed->referent()->unqualified()
                                                     : agreed->unqualified())) disagree = true;
            }
        // **[over.ics.list] with [over.ics.rank]/3 (R21)**: where the candidates disagree, the list is
        // made for the one it converts to best - an initializer_list<X> before anything else, then the
        // worse of its elements' conversions; a class it constructs is a user-defined conversion.
        if (disagree) {
            const Init *in = bracedInitOf(*args[i]);
            const Type *best = nullptr;
            int bestKlass = 2;
            Rank bestRank = Rank::None;
            bool tie = false;
            bool bestFromTemplate = false;
            for (std::size_t k = 0; k < asked.size(); k++) {
                const Signature &f = functions_[asked[k]];
                if (i >= f.params.size() || in == nullptr) continue;
                const Type *p = (f.params[i]->isReference() ? f.params[i]->referent() : f.params[i])->unqualified();
                int klass = 1;
                Rank r = Rank::UserDefined;
                if (isInitializerListType(p)) {
                    klass = 0;
                    r = Rank::Identity;
                    const Type *x = p->templateArgs().empty() ? nullptr : p->templateArgs()[0].type;
                    for (std::size_t e = 0; e < in->items.size() && x != nullptr; e++) {
                        if (in->items[e].isList || in->items[e].value == nullptr) { r = Rank::UserDefined; continue; }
                        const Rank er = rankArgument(*in->items[e].value, x);
                        if (er > r) r = er;
                    }
                } else if (!p->isStructOrUnion()) {
                    if (in->items.empty()) r = Rank::Identity;
                    else if (in->items.size() == 1 && !in->items[0].isList && in->items[0].value != nullptr)
                        r = rankArgument(*in->items[0].value, p);
                    else r = Rank::None;
                }
                if (r == Rank::None) continue;
                // An initializer_list beats the rest outright; among the rest the rank decides.
                if (best == nullptr || klass < bestKlass || (klass == bestKlass && r < bestRank)) {
                    best = f.params[i]; bestKlass = klass; bestRank = r; tie = false;
                    bestFromTemplate = f.fromTemplate;
                } else if (klass == bestKlass && r == bestRank &&
                           (f.params[i]->isReference() ? f.params[i]->referent() : f.params[i])->unqualified() !=
                           (best->isReference() ? best->referent() : best)->unqualified()) {
                    // [over.match.best]/1: a function that is not a template's specialization wins the tie - and a
                    // specialization made for an earlier call is no candidate for a list its parameter cannot be
                    // deduced from: `m.insert({ "e", 5 })` after `m.insert(std::make_pair("d", 4))` (1.76).
                    if (f.fromTemplate != bestFromTemplate) {
                        if (bestFromTemplate) { best = f.params[i]; bestFromTemplate = false; tie = false; }
                    } else {
                        tie = true;
                    }
                }
            }
            if (best == nullptr || tie)
                src_.fail(pos, "a braced list is given as argument " + std::to_string(i + 1) + " of '" +
                               key + "', and " + (tie ? "two of its overloads take it equally well"
                                                      : "none of its overloads can be initialised from it") +
                               " - [over.ics.list]");
            agreed = best;
        }
        params.push_back(agreed);
    }
    for (std::size_t i = 0; i < args.size(); i++)
        if (isBracedPlaceholder(*args[i]) && params[i] == nullptr)
            src_.fail(pos, "a braced list is given as argument " + std::to_string(i + 1) + " of '" +
                           key + "', and no overload has a parameter there");
    buildBracedArguments(params, args, pos);
}

// [over.over]/1 with the overload set of the call itself: a candidate whose parameter no
// function of the argument's set fits is not viable for it, and the viable ones must agree.
void Parser::pickOverloadArguments(const std::string &key, std::vector<ExprPtr> &args,
                                   const Type *object) {
    bool any = false;
    for (std::size_t i = 0; i < args.size() && !any; i++) any = overloadArgOf(*args[i]) != nullptr;
    if (!any) return;
    const std::string name = object != nullptr ? key : qualifyForLookup(key, &Parser::hasFunctionNamed);
    const std::vector<std::size_t> *callee = overloadsOf(name);
    if (callee == nullptr) return;
    for (std::size_t i = 0; i < args.size(); i++) {
        const OverloadArg *oa = overloadArgOf(*args[i]);
        if (oa == nullptr) continue;
        const Signature *picked = nullptr;
        std::string pickedSymbol;
        const Type *param = nullptr;
        bool differ = false;
        // Each candidate's parameter here and the ranks of the written arguments it was given.
        std::vector<const Type *> targets;
        std::vector<std::vector<Rank> > ranks;
        for (std::size_t k = 0; k < callee->size(); k++) {
            const Signature &f = functions_[(*callee)[k]];
            if (f.variadic ? args.size() < leastArguments(f)
                           : (args.size() > f.params.size() || args.size() < leastArguments(f))) continue;
            if (i >= f.params.size()) continue;
            // A candidate another written argument rules out picks nothing, so it cannot disagree.
            bool viable = true;
            for (std::size_t j = 0; j < args.size() && j < f.params.size() && viable; j++)
                if (j != i && overloadArgOf(*args[j]) == nullptr && !isBracedPlaceholder(*args[j]))
                    viable = rankArgument(*args[j], f.params[j]) != Rank::None;
            if (!viable) continue;
            // A template's specializations are made for the parameter, which may move the tables read here.
            const Type *target = f.params[i];
            std::vector<std::size_t> set = oa->set;
            if (!oa->templates.empty()) {
                const OverloadArg with = *oa;
                const std::vector<std::size_t> made = specializationsFor(with.templates, target, with.pos);
                set.insert(set.end(), made.begin(), made.end());
                oa = overloadArgOf(*args[i]);
                callee = overloadsOf(name);
            }
            const Signature *one = pickByTarget(set, oa->cls, target);
            if (one == nullptr) continue;
            if (picked != nullptr && one->symbol != pickedSymbol) differ = true;
            picked = one;
            pickedSymbol = one->symbol;
            param = target;
            targets.push_back(target);
            ranks.push_back(std::vector<Rank>());
            const Signature &g = functions_[(*callee)[k]];
            for (std::size_t j = 0; j < args.size() && j < g.params.size(); j++)
                ranks.back().push_back(j != i && overloadArgOf(*args[j]) == nullptr && !isBracedPlaceholder(*args[j])
                                           ? rankArgument(*args[j], g.params[j]) : Rank::Identity);
        }
        // [over.match.best]: each candidate takes its own function here, and the other arguments decide.
        for (std::size_t c = 0; differ && c < ranks.size(); c++) {
            bool beatsAll = true;
            for (std::size_t d = 0; d < ranks.size() && beatsAll; d++) {
                if (d == c) continue;
                bool better = false, worse = false;
                for (std::size_t j = 0; j < ranks[c].size() && j < ranks[d].size(); j++) {
                    if (ranks[c][j] < ranks[d][j]) better = true;
                    if (ranks[c][j] > ranks[d][j]) worse = true;
                }
                beatsAll = better && !worse;
            }
            if (beatsAll) {
                param = targets[c];
                differ = false;
            }
        }
        const std::string head = "'" + oa->key + "' names " + std::to_string(oa->set.size()) +
                                 " functions, and ";
        if (differ)
            src_.fail(oa->pos, head + "the overloads of '" + key + "' take different ones of them as "
                               "argument " + std::to_string(i + 1) + " - initialise a pointer of the "
                               "type wanted and pass that");
        if (picked == nullptr)
            src_.fail(oa->pos, head + "no overload of '" + key + "' has a parameter of the type of "
                               "one of them as argument " + std::to_string(i + 1));
        args[i] = overloadArgumentFor(param, *overloadArgOf(*args[i]));
    }
}

Parser::Signature Parser::resolveOverloadBraced(const std::string &key, std::vector<ExprPtr> &args,
                                                std::size_t pos, const Type *object,
                                                bool objectLvalue) {
    buildBracedArguments(key, args, pos, object);
    return resolveOverload(key, args, pos, object, objectLvalue);
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
    buildBracedArguments(params, args, pos);
    return completeCall(name, symbol, std::move(callee), returns, params,
                        variadic, pos, std::move(args));
}

// [except.ctor]/1: the object an initialiser built is complete when its
// temporaries go, and is destroyed if one of their destructors throws.
void Parser::flushOverObject(std::vector<StmtPtr> &into, const Alive &made, std::size_t pos) {
    bool throwing = false;
    for (const Temporary &t : pendingTemps_) throwing = throwing || destructorMayThrow(t.type);
    if (!throwing || destructorOf(made.cls) == nullptr) {
        flushTemporaries(into, true);
        return;
    }
    std::vector<StmtPtr> ending, rest;
    flushTemporaries(ending, true);
    destroyObject(rest, made, pos);
    Block *b = new Block(std::move(rest));
    b->setScope(-1);
    std::vector<StmtPtr> region;
    region.push_back(cleanupRegion(std::move(ending), StmtPtr(b)));
    Block *alone = new Block(std::move(region));
    alone->setScope(-1);
    into.push_back(StmtPtr(alone));
}

void Parser::flushTemporaries(std::vector<StmtPtr> &into, bool inBlock) {
    if (pendingTemps_.empty()) return;
    std::vector<Temporary> mine;
    mine.swap(pendingTemps_);
    for (std::size_t k = mine.size(); k-- > 0; ) {
        const Signature *dtor = destructorOf(mine[k].type);
        if (dtor == nullptr) continue;
        StmtPtr call(new ExprStmt(destructorCall(temporaryAddress(mine[k]), *dtor, 0)));
        // **Under its guard, and cleared with it.**
        if (mine[k].flag != 0) {
            ExprPtr live(Var::local("$guard", mine[k].flag));
            live->setType(types_.intType());
            std::vector<StmtPtr> both;
            both.push_back(StmtPtr(new ExprStmt(setGuard(mine[k].flag, 0))));
            both.push_back(std::move(call));
            call.reset(new If(std::move(live),
                              StmtPtr(new Block(std::move(both))), StmtPtr()));
            // A later temporary's destructor may throw over it: the block's region destroys what is left.
            if (inBlock) statementTemps_.push_back(mine[k]);
        }
        into.push_back(std::move(call));
    }
}

// Where a temporary is: its own slot, or - an element of an array a slot points at - that element.
ExprPtr Parser::temporaryAddress(const Temporary &t) {
    const Type *ptr = types_.pointerTo(t.type);
    if (!t.newElement) {
        ExprPtr what(Var::local("$copy", t.slot));
        what->setType(t.type);
        ExprPtr at(new Unary('&', std::move(what)));
        at->setType(ptr);
        return at;
    }
    const Type *chars = types_.pointerTo(types_.get(Kind::Char));
    ExprPtr held;
    if (t.elementInFrame) {
        ExprPtr whole(Var::local("$copy", t.slot));
        whole->setType(t.type);
        held.reset(new Unary('&', std::move(whole)));
        held->setType(ptr);
        held.reset(new Cast(chars, std::move(held)));
    } else {
        held.reset(Var::local("$copy", t.slot));
    }
    held->setType(chars);
    ExprPtr skip(new Num(t.newIndex * static_cast<long long>(t.type->size(target_))));
    skip->setType(types_.intType());
    ExprPtr byte(new Binary(BinOp::Add, std::move(held), std::move(skip)));
    byte->setType(chars);
    ExprPtr at(new Cast(ptr, std::move(byte)));
    at->setType(ptr);
    return at;
}

// **Take one temporary off the pending list**, because something else has
// become responsible for it.
bool Parser::callsNoexceptPointer(const Expr &callee) const {
    const Expr *e = &callee;
    for (;;) {
        if (const Cast *c = dynamic_cast<const Cast *>(e)) { e = &c->value(); continue; }
        if (const Unary *u = dynamic_cast<const Unary *>(e))
            if (u->op() == '*') { e = &u->operand(); continue; }
        break;
    }
    if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(e)) {
        const Type *cls = m->object().type() != nullptr ? m->object().type()->unqualified() : nullptr;
        const Member *f = cls != nullptr && cls->isStructOrUnion() ? cls->findMember(m->name()) : nullptr;
        if (f == nullptr) return false;
        const Type *owner = f->declaredIn != nullptr ? f->declaredIn->unqualified() : cls;
        return noexceptPointers_.count(owner->tag() + "::" + m->name()) != 0;
    }
    const Var *v = dynamic_cast<const Var *>(e);
    if (v == nullptr) return false;
    if (const Local *l = findLocal(v->name())) return l->noexceptPointer;
    return noexceptPointers_.count(v->name()) != 0;
}

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

// **The pending temporary a bound address names**: its pendingTemps_ index or
// -1, walking through commas, `*`/`&`, casts and member accesses (a subobject
// keeps the whole temporary, [class.temporary]/5), or a saved base's store.
int Parser::pendingTemporaryOf(const Expr &e) const {
    if (const Comma *c = dynamic_cast<const Comma *>(&e)) {
        const int right = pendingTemporaryOf(c->right());
        if (right >= 0) return right;
        if (dynamic_cast<const Conditional *>(&c->right()) != nullptr ||
            dynamic_cast<const Cast *>(&c->right()) != nullptr)
            if (const Assign *a = dynamic_cast<const Assign *>(&c->left()))
                return pendingTemporaryOf(a->value());
        return -1;
    }
    if (const Unary *u = dynamic_cast<const Unary *>(&e))
        return u->op() == '*' || u->op() == '&' ? pendingTemporaryOf(u->operand()) : -1;
    if (const Cast *c = dynamic_cast<const Cast *>(&e)) return pendingTemporaryOf(c->value());
    if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(&e))
        return pendingTemporaryOf(m->object());
    int slot = 0;
    if (const Var *v = dynamic_cast<const Var *>(&e)) {
        if (!v->isLocal()) return -1;
        slot = v->offset();
    } else if (const Call *c = dynamic_cast<const Call *>(&e)) {
        slot = c->resultSlot();
    } else {
        return -1;
    }
    if (slot == 0) return -1;
    for (std::size_t i = 0; i < pendingTemps_.size(); i++)
        if (pendingTemps_[i].slot == slot && !pendingTemps_[i].newElement) return static_cast<int>(i);
    return -1;
}

// **[class.temporary]/5: a temporary bound to a reference lives as long as the
// reference does**, not to the end of the full expression that made it.
bool Parser::extendTemporary(const Expr &addr, const std::string &name) {
    const int i = pendingTemporaryOf(addr);
    if (i < 0) return false;
    const int slot = pendingTemps_[i].slot;
    const Type *cls = pendingTemps_[i].type;
    pendingTemps_.erase(pendingTemps_.begin() + i);
    // The guard flag goes with the entry.
    if (destructorOf(cls) != nullptr)
        alive_.push_back(Alive{ name, slot, cls });
    return true;
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
        ExprPtr gone = destructorCall(temporaryAddress(mine[k]), *dtor, 0);
        // Clear the guard with the destruction, so that a pad later in the
        // same block does not destroy what this statement already has - and
        // ask it first, for the same reason `flushTemporaries` does.
        if (mine[k].flag != 0) {
            ExprPtr cleared(new Comma(setGuard(mine[k].flag, 0), std::move(gone)));
            cleared->setType(types_.get(Kind::Void));
            ExprPtr nought(new Num(0LL));
            nought->setType(types_.intType());
            ExprPtr clear(new Comma(std::move(cleared), std::move(nought)));
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
    // [over.best.ics]/6: a derived object is copied into a base parameter from its base subobject.
    if (arg->type() != nullptr && arg->type()->unqualified() != cls && isGlvalue(*arg) &&
        arg->type()->unqualified()->isStructOrUnion() && accessibleBaseOffset(arg->type(), cls) >= 0)
        arg = baseSubobject(std::move(arg), cls);
    if (cc->access != Access::Public && !insideAccessOf(cls, cc->access) &&
        !isFriendOf(cls))
        src_.fail(pos, "'" + cls->describe() + "' is passed by value as " + what +
                       ", which copies it, and its copy constructor is " +
                       (cc->access == Access::Private ? "private" : "protected"));
    checkAssignable(*arg, cls, pos, what);
    // [expr.unary.noexcept]/3: the copy or move that initialises a by-value parameter is a call of the
    // expression, elided or not ([class.copy]/31 leaves the constructor odr-used) - 1.77.
    if (!cc->isNoexcept) mayThrow_++;

    const Type *ptr = types_.pointerTo(cls);
    // **[class.copy]/31: a temporary of the class is the parameter's object**, `take(K(5))`: its address is
    // what is passed, and no copy or move constructor runs (1.78). Who destroys it stays as the ABI has it for
    // a copy: the caller at the end of the full expression, or - Microsoft's - the function called.
    if (freshTemporarySlot(*arg, cls) != 0) {
        if (target_.microsoftNames()) adoptTemporary(freshTemporarySlot(*arg, cls));
        ExprPtr at(new Unary('&', std::move(arg)));
        at->setType(ptr);
        return at;
    }
    const int tmp = allocateFrameSlot(cls);
    int guard = 0;
    if (destructorOf(cls) != nullptr) {
        guard = guardFlag();
        destroy.push_back(Temporary{ tmp, cls, guard });
    }

    // **Elision, where the argument is already one of these coming back through a
    // hidden pointer.** The call builds its result straight into the temporary this
    // argument needs, so no copy constructor runs - as clang does at -O0.
    if (Call *made = resultCall(arg.get())) {
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

// [class.dtor]/11 with [class.access]: an object whose destructor the code here cannot call is not made here.
void Parser::refuseInaccessibleDestructor(const Type *t, std::size_t pos, const std::string &what) const {
    if (t->isReference()) return;
    const Type *cls = t->innermostElement()->unqualified();
    if (!cls->isStructOrUnion()) return;
    const Signature *dtor = destructorOf(cls);
    if (dtor == nullptr || dtor->access == Access::Public || insideAccessOf(cls, dtor->access) || isFriendOf(cls))
        return;
    src_.fail(pos, what + " would be destroyed by the " +
                   (dtor->access == Access::Private ? "private" : "protected") + " destructor of '" +
                   cls->describe() + "', which is not accessible here");
}

// The program's `main` is defined in this unit: a runtime's start-up, which calls one defined
// elsewhere, is the implementation and not the program [basic.start.main]/3 speaks of.
bool Parser::mainDefinedHere() const {
    if (const std::vector<std::size_t> *set = overloadsOf("main"))
        for (std::size_t i : *set)
            if (functions_[i].cLinkage && functions_[i].defined) return true;
    return false;
}

// [dcl.init]/15: a parameter copy-initialised from its argument - also what `__is_convertible_to` asks.
ExprPtr Parser::copyInitialise(const Type *param, ExprPtr arg, std::size_t pos, const std::string &what,
                               std::vector<Temporary> &destroy) {
    // **[over.ics.user]: a converting constructor may be called to make an
    // argument.**
    if (ExprPtr made = userConversion(param, arg, pos))
        arg = std::move(made);
    if (param->isReference()) return bindReference(param, std::move(arg), pos, what);
    // **A class whose copy is a constructor call is copied by the caller**,
    // into a temporary the caller owns, and the callee receives its address.
    // Measured on all three targets: clang and cl copy at the call site.
    if (passedByAddress(param)) return materialiseCopy(param, std::move(arg), pos, what, destroy);
    arg = decay(std::move(arg));
    checkAssignable(*arg, param, pos, what);
    return convert(std::move(arg), param);
}

ExprPtr Parser::completeCall(const std::string &name, const std::string &symbol,
                             ExprPtr callee, const Type *returns,
                             const std::vector<const Type *> &params,
                             bool variadic, std::size_t pos,
                             std::vector<ExprPtr> args, bool hasThis,
                             int vbInit) {
    if (callee == nullptr && symbol == "main" && name == "main") {
        if (mainDefinedHere())
            src_.fail(pos, "'main' cannot be called - [basic.start.main]/3 forbids using it within the program");
        mainUses_.push_back(pos);
    }
    if (callee == nullptr && symbol == "__cxx1_flag_exchange") current_->usesFlagExchange = true;
    if (callee == nullptr && symbol.compare(0, 14, "__cxx1_atomic_") == 0) current_->atomicHelpers.insert(symbol);
    noteUnsetArguments(args, params);
    // **A member class of a class template returned by the call is instantiated here** - [expr.call]/11 wants a
    // class prvalue complete, and `bits[3] = true` then finds bitset<N>::reference's operator=.
    if (returns != nullptr) realizeMemberClass(returns->isReference() ? returns->referent() : returns, true);
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
    // A list nobody built reaches here from a road that never asked its parameter's type.
    refuseOverloadArguments(args, name);
    for (std::size_t i = 0; i < args.size(); i++)
        if (isBracedPlaceholder(*args[i]))
            src_.fail(pos, "a braced list as argument " + std::to_string(i + 1) + " of '" + name +
                           "' is not supported yet on this road - name the type, 'T{...}'");

    // **A call through a pointer promises nothing** - unless it is a virtual function's slot, whose
    // function is known and noexcept ([except.spec]/5: no override may promise less; 1.77).
    if (callee != nullptr && !calleeNoexcept_ && !callsNoexceptPointer(*callee)) mayThrow_++;
    calleeNoexcept_ = false;

    // Temporaries this call makes for its by-value class arguments, and which this call therefore has to destroy once it returns.
    std::vector<Temporary> destroy;

    for (std::size_t i = 0; i < args.size(); i++) {
        if (i >= params.size()) {
            args[i] = defaultPromote(decay(std::move(args[i])));
            continue;
        }
        std::string what = "argument " + std::to_string(i + 1) + " of '" + name + "'";
        args[i] = copyInitialise(params[i], std::move(args[i]), pos, what, destroy);
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
    // **`_alloca` and `alloca` with C linkage are the compiler's own**, as cl's are: <malloc.h> declares
    // `void *_alloca(size_t)` and spells alloca as it, and each backend lowers the call where it stands (9.4).
    if (call->callee() == nullptr && !hasThis && call->args().size() == 1 && returns->isPointer() &&
        (symbol == "_alloca" || symbol == "alloca"))
        call->setAlloca();
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

// **The call a class prvalue is, seen through the commas in front of it.** A virtual call is `(.vc = &obj,
// call)` - the object's address saved for the vtable's read and for `this` - and until 1.81 only a bare call
// was seen: `H q = n.make();` for a virtual make() copied the result's bytes into another temporary, moved q
// from that, and destroyed the result, which the copy still shared.
Call *Parser::resultCall(const Expr *e) {
    while (const Comma *c = dynamic_cast<const Comma *>(e)) e = &c->right();
    return const_cast<Call *>(dynamic_cast<const Call *>(e));
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

// **The class whose scope declares `name`**, a member template counting as a declaration:
// `q * r` for a `Q : V` finds V's `template <int K> operator*`, [class.member.lookup].
const Type *Parser::declaringClassOf(const Type *cls, const std::string &name) const {
    if (cls == nullptr) return nullptr;
    const Type *c = cls->unqualified();
    if (overloadsOf(c->tag() + "::" + name) != nullptr || isMemberTemplate(c, name)) return c;
    for (const Type::BaseSpec &b : c->bases())
        if (const Type *found = declaringClassOf(b.type, name)) return found;
    return nullptr;
}

// Where `base` sits in `derived` along non-virtual bases, whatever their access; -1 where it is not
// one, or only behind a virtual base.
static int nonVirtualBaseOffset(const Type *derived, const Type *base) {
    const Type *d = derived->unqualified();
    if (d == base->unqualified()) return 0;
    for (const Type::BaseSpec &s : d->bases()) {
        if (s.isVirtual) continue;
        const int deeper = nonVirtualBaseOffset(s.type, base);
        if (deeper >= 0) return s.offset + deeper;
    }
    return -1;
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

    // The object's value category is what a ref-qualifier is ranked against, [over.match.funcs]/4.
    const Signature &sig = resolveOverloadBraced(key, args, pos, cls, objectIsLvalue(*object));
    applyDefaults(sig, args, pos);

    // A base's function that a using-declaration put in `owner`'s set is still the base's, and `this` is
    // that base's subobject; its access is the using-declaration's, [namespace.udecl]/15.
    Access access = sig.access;
    const Type *usingClass = nullptr;
    if (!sig.owner.empty() && sig.owner != owner->tag()) {
        for (const Type::BaseSpec &b : plain->bases())
            if (b.type->unqualified()->tag() == sig.owner) owner = b.type->unqualified();
        usingClass = usingAccessOf(plain, owner, name, &access);
    }

    // Now there IS an inside, and this is where it starts to mean something:
    // a private member is reachable from another member of the same class.
    const bool granted = usingClass != nullptr && (access == Access::Public || insideAccessOf(usingClass, access) ||
                                                   isFriendOf(usingClass));
    if (!granted && access != Access::Public && !insideAccessOf(plain, access) &&
        !insideAccessOf(owner, access) && !isFriendOf(plain) &&
        !isFriendOf(owner)) {
        const char *how = access == Access::Private ? "private" : "protected";
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
    if (cls->isVolatile() && !sig.volatileThis)
        src_.fail(pos, "'" + name + "' is not a volatile member function, and this object is "
                       "volatile - [over.match.funcs]/4 binds a volatile object's 'this' to a "
                       "volatile member function alone");
    const Type *pointee = sig.constThis ? types_.withConst(self) : self;
    if (sig.volatileThis) pointee = types_.withVolatile(pointee);
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
    // **On Microsoft `this` is the vfptr's subobject** (Phase 6): a dispatched call passes the one
    // whose table holds the slot, a qualified call the one its function's adjustor names.
    const bool ms = target_.microsoftNames();
    int msShift = 0, msIndex = -1;
    if (ms && sig.isVirtual && forceOwner == nullptr) {
        const int ownerAt = nonVirtualBaseOffset(plain, owner);
        const std::vector<MsVftable> tables = msVftables(plain);
        for (std::size_t t = 0; t < tables.size() && msIndex < 0; t++)
            for (std::size_t i = 0; i < tables[t].slots.size(); i++)
                if (overrides(tables[t].slots[i], name, sig.params, sig.constThis, sig.refThis)) {
                    // In a virtual base the table is where the object's vbtable says (M3).
                    if (tables[t].vbase != nullptr) {
                        addr = msShifted(convert(std::move(addr), types_.pointerTo(tables[t].vbase)),
                                         tables[t].atInVbase);
                        addr.reset(new Cast(thisType, std::move(addr)));
                        addr->setType(thisType);
                    } else if (ownerAt >= 0) {
                        msShift = tables[t].at - ownerAt;
                    } else {
                        continue;
                    }
                    msIndex = static_cast<int>(i);
                    break;
                }
    } else if (ms && sig.isVirtual) {
        msShift = msThisAdjust(sig.symbol);
    }
    if (msShift != 0) {
        const Type *chars = types_.pointerTo(types_.get(Kind::Char));
        ExprPtr asChars(new Cast(chars, std::move(addr)));
        asChars->setType(chars);
        ExprPtr by(new Num(static_cast<long long>(msShift)));
        by->setType(types_.intType());
        ExprPtr moved(new Binary(BinOp::Add, std::move(asChars), std::move(by)));
        moved->setType(chars);
        addr.reset(new Cast(thisType, std::move(moved)));
        addr->setType(thisType);
    }
    // **A qualified call is never dispatched** - [expr.call]/1 - which is the
    // whole reason an override writes `Base::f(...)` to reach what it replaced.
    if (sig.isVirtual && forceOwner == nullptr) {
        int index = msIndex;
        const std::vector<VSlot> &slots = vtables_[plain->tag()];
        for (std::size_t i = 0; i < slots.size() && index < 0; i++) {
            if (overrides(slots[i], name, sig.params, sig.constThis, sig.refThis, sig.volatileThis)) {
                index = static_cast<int>(i);
                break;
            }
        }
        // **A function of a base off the primary chain** - a second base, a
        // virtual one - is not in this class's table: `addr` is that base's
        // subobject by now, and its own vptr and slot are what dispatch it.
        if (index < 0 && owner != plain && !ms) {
            const std::vector<VSlot> &theirs = vtables_[owner->tag()];
            for (std::size_t i = 0; i < theirs.size(); i++) {
                if (overrides(theirs[i], name, sig.params, sig.constThis, sig.refThis, sig.volatileThis)) {
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
    calleeNoexcept_ = callee != nullptr && sig.isNoexcept;
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
    if (isFriendClassOf(cls)) return true;
    if (cls == nullptr || currentFunction_.empty()) return false;
    std::map<std::string, std::vector<std::string> >::const_iterator it =
        friends_.find(cls->unqualified()->tag());
    if (it == friends_.end()) return false;
    for (std::size_t i = 0; i < it->second.size(); i++) {
        if (it->second[i] == currentFunction_) return true;
        // `template <class T> friend int tpeek(const B &);`: every specialization's body (R12).
        if (!instantiationOf_.empty() && it->second[i] == "template " + instantiationOf_) return true;
    }
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
    // **And so does a class nested in it** - [class.access.nest]/1, C++11's rule: a nested class is a
    // member, and a member has the access of any other (1.27, 2026-10-21, review of 1.25, item 3:
    // `Outer::Inner::peek(const Outer &o)` reading `o.secret` found "no member").
    for (std::size_t i = 0; i <= classStack_.size(); i++) {
        const Type *here = i < classStack_.size() ? classStack_[i] : currentClass_;
        for (const Type *e = here != nullptr ? here->enclosing() : nullptr; e != nullptr; e = e->enclosing())
            if (e->unqualified() == want) return true;
    }
    if (currentClass_ == nullptr) return false;
    if (currentClass_ == want) return true;
    if (access == Access::Protected && derivesFrom(currentClass_, want))
        return true;
    std::map<std::string, const Type *>::const_iterator outer =
        closureOuter_.find(currentClass_->unqualified()->tag());
    if (outer != closureOuter_.end() && outer->second->unqualified() == want) return true;
    // **[class.access]/2: a local class of a member function has the function's access** - the
    // class's own, and its enclosing classes' (R10: `s.v` read in one was "private").
    int hops = 0;
    for (const Type *c = currentClass_->unqualified(); c != nullptr && hops < 16; hops++) {
        std::map<std::string, const Type *>::const_iterator scope = localClassScope_.find(c->tag());
        if (scope == localClassScope_.end()) break;
        for (const Type *e = scope->second->unqualified(); e != nullptr; e = e->enclosing())
            if (e->unqualified() == want) return true;
        if (access == Access::Protected && derivesFrom(scope->second, want)) return true;
        c = scope->second->unqualified();
    }
    return false;
}

bool Parser::accessibleFrom(const Type *from, const Type *owner,
                            Access a) const {
    if (a == Access::Public) return true;
    if (from == nullptr || owner == nullptr) return false;
    const Type *want = owner->unqualified();
    if (from->unqualified() == want) return true;
    return a == Access::Protected && derivesFrom(from->unqualified(), want);
}

// [class.access.base]/4: each step down is accessible where its base is public, or the code
// is a member or friend of the class that names it, or - a protected base - a member of a
// class derived from that one. A chain of accessible steps is an accessible base, (d).
bool Parser::baseStepAccessible(const Type *d, Access how) const {
    if (how == Access::Public) return true;
    if (insideAccessOf(d, Access::Private) || isFriendOf(d)) return true;
    return how == Access::Protected && insideAccessOf(d, Access::Protected);
}

int Parser::accessibleBaseOffset(const Type *derived, const Type *base) const {
    if (derived == nullptr || base == nullptr) return -1;
    const Type *d = derived->unqualified();
    if (d == base->unqualified()) return 0;
    const std::vector<Type::BaseSpec> &bases = d->bases();
    for (std::size_t i = 0; i < bases.size(); i++) {
        if (!baseStepAccessible(d, bases[i].access)) continue;
        const int deeper = accessibleBaseOffset(bases[i].type, base);
        if (deeper >= 0) return bases[i].offset + deeper;
    }
    return -1;
}

// What a member declared in `decl` with `own` access is as a member of `cls` - the
// least restrictive path, a private one going no further; false where it is not one.
static bool accessAsMemberOf(const Type *cls, const Type *decl, Access own, Access *out) {
    if (cls == decl) { *out = own; return true; }
    bool found = false;
    for (const Type::BaseSpec &b : cls->bases()) {
        const Type *bt = b.type->unqualified();
        if (!b.direct || (bt != decl && !derivesFrom(bt, decl))) continue;
        Access inB;
        if (!accessAsMemberOf(bt, decl, own, &inB) || inB == Access::Private) continue;
        const Access here = inB > b.access ? inB : b.access;
        if (!found || here < *out) *out = here;
        found = true;
    }
    return found;
}

// [class.access.base]/5: a member named in `n` is accessible as a member of `n` (a)-(c), or
// through a base of `n` accessible here in which it is accessible, (d).
bool Parser::memberReachable(const Type *n, const Type *decl, Access own) const {
    Access a;
    if (accessAsMemberOf(n, decl, own, &a)) {
        if (a == Access::Public) return true;
        if (insideAccessOf(n, Access::Private) || isFriendOf(n)) return true;
        if (a == Access::Protected && insideAccessOf(n, Access::Protected)) return true;
    }
    for (const Type::BaseSpec &b : n->bases()) {
        const Type *bt = b.type->unqualified();
        if (!b.direct || (bt != decl && !derivesFrom(bt, decl))) continue;
        if (baseStepAccessible(n, b.access) && memberReachable(bt, decl, own)) return true;
    }
    return false;
}

void Parser::checkAccessible(const Type *object, const Member &m,
                             std::size_t pos) const {
    // [namespace.udecl]/15: a using-declaration on the way down makes it a member of that class with its access.
    if (object != nullptr && m.declaredIn != nullptr) {
        Access via;
        if (const Type *c = usingAccessOf(object, m.declaredIn->unqualified(), m.name, &via)) {
            if (via == Access::Public || insideAccessOf(c, via) || isFriendOf(c)) return;
            src_.fail(pos, "'" + m.name + "' is " + (via == Access::Private ? "private" : "protected") +
                           " in '" + c->describe() + "', whose using-declaration names it there - it can be "
                           "named only from inside the class, and this is outside it");
        }
    }
    if (m.access == Access::Public) return;
    // **Asked about the class that declared it**, not the one it was reached
    // through: a base's members are copied down, so a private member of `B`
    // read through a `D` was asked about `D` and answered yes from inside `D`.
    const Type *owner = m.declaredIn != nullptr ? m.declaredIn : object;
    // [class.protected]: a protected member reached from a derived class only through that class -
    // `b.p` for a `B &b` in a member of `D : B`, or through a sibling `D1`, is not access D has.
    if (m.access == Access::Protected && object != nullptr && currentClass_ != nullptr &&
        insideAccessOf(owner, m.access) && !insideAccessOf(owner, Access::Private) && !isFriendOf(owner)) {
        const Type *here = currentClass_->unqualified();
        const Type *through = object->unqualified();
        if (through != here && !derivesFrom(through, here) && derivesFrom(here, owner->unqualified()))
            src_.fail(pos, "'" + m.name + "' is a protected member of '" + owner->unqualified()->describe() +
                           "', and '" + here->describe() + "' reaches it only through an object of its own "
                           "class - [class.protected]");
    }
    if (insideAccessOf(owner, m.access)) return;
    if (isFriendOf(owner)) return;
    // **A member of a non-public base** carries the path's access in `m.access`, which says
    // nothing about the classes on the way; the walk asks [class.access.base]/5 of each.
    const Member *own = nullptr;
    if (m.declaredIn != nullptr)
        for (const Member &o : m.declaredIn->unqualified()->members())
            if (o.name == m.name && o.declaredIn == nullptr) own = &o;
    if (own != nullptr && object != nullptr &&
        memberReachable(object->unqualified(), m.declaredIn->unqualified(), own->access))
        return;
    // [class.access.base]/5, [class.protected]: a friend of a class between the object's and
    // the declaring one names a protected member through that object - never through a base.
    if (m.access == Access::Protected && object != nullptr && owner != object)
        for (const Type *c = object->unqualified(); c != nullptr;) {
            if (c != owner->unqualified() && derivesFrom(c, owner->unqualified()) && isFriendOf(c))
                return;
            const Type *up = nullptr;
            for (const Type::BaseSpec &b : c->bases())
                if (b.type->unqualified() == owner->unqualified() ||
                    derivesFrom(b.type, owner->unqualified()))
                    up = b.type->unqualified();
            c = up;
        }
    // No fallback to the class it was reached *through*.
    if (own != nullptr && own->access < m.access && object != nullptr)
        src_.fail(pos, "'" + m.name + "' is a member of '" + owner->describe() + "' that '" +
                       object->unqualified()->describe() + "' reaches through a base not "
                       "accessible here - [class.access.base]/5");
    const char *how = m.access == Access::Private ? "private" : "protected";
    // Named by the class that declared it: saying it is private in the derived
    // class sends the reader to a class whose source does not mention it.
    src_.fail(pos, "'" + m.name + "' is " + how + " in '" + owner->describe() +
                   "' - it can be named only from inside the class, and this "
                   "is outside it");
}

// **`S::T` for a typedef or alias S declares private or protected**, [class.access]/1: asked of the
// class whose key holds it, S's own or a base's, as checkAccessible asks of the declaring class. A
// lambda's body has the access of the member function it was written in, [expr.prim.lambda]/7.
void Parser::checkMemberTypeAccess(const Type *cls, const std::string &name, std::size_t pos) const {
    if (cls == nullptr || !cls->unqualified()->isStructOrUnion()) return;
    std::vector<const Type *> walk(1, cls->unqualified());
    for (std::size_t i = 0; i < walk.size(); i++) {
        const std::string key = walk[i]->tag() + "::" + name;
        if (typedefIndex_.find(key) == typedefIndex_.end()) {
            for (const Type::BaseSpec &b : walk[i]->bases()) walk.push_back(b.type->unqualified());
            continue;
        }
        std::unordered_map<std::string, Access>::const_iterator it = memberTypeAccess_.find(key);
        if (it != memberTypeAccess_.end()) checkNamedMemberAccess(walk[i], it->second, name, pos);
        return;
    }
}

// `S::a` for an enumerator of an enumeration S declares private or protected, the same way.
void Parser::checkMemberEnumAccess(const Type *cls, const std::string &name, std::size_t pos) const {
    if (cls == nullptr || !cls->unqualified()->isStructOrUnion()) return;
    std::vector<const Type *> walk(1, cls->unqualified());
    for (std::size_t i = 0; i < walk.size(); i++) {
        std::unordered_map<std::string, std::size_t>::const_iterator it = enumIndex_.find(walk[i]->tag() + "::" + name);
        if (it == enumIndex_.end()) {
            for (const Type::BaseSpec &b : walk[i]->bases()) walk.push_back(b.type->unqualified());
            continue;
        }
        checkNamedMemberAccess(walk[i], enums_[it->second].access, name, pos);
        return;
    }
}

void Parser::checkNamedMemberAccess(const Type *owner, Access access, const std::string &name,
                                    std::size_t pos) const {
    if (access == Access::Public || insideAccessOf(owner, access) || isFriendOf(owner)) return;
    for (const Type *c = lambdaScope(); c != nullptr; c = c->enclosing())
        if (c->unqualified() == owner || (access == Access::Protected && derivesFrom(c->unqualified(), owner)))
            return;
    src_.fail(pos, "'" + name + "' is " + (access == Access::Private ? "private" : "protected") + " in '" +
                   owner->describe() + "' - it can be named only from inside the class, and this is outside it");
}
