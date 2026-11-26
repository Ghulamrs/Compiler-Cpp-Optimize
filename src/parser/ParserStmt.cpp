// The parser: statements. Declarations as statements, every control-flow
// statement including try and the range-based for, and the goto labels
// resolved at a function's end.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>

// **The initialiser inside `(...)` of a direct-initialised scalar.**
// [dcl.constexpr]/9: a local object of class type is folded from the statements that build it at run time.
void Parser::foldLocalObject(const Declared &d, int off, const std::vector<StmtPtr> &inits, std::size_t from) {
    std::vector<const Stmt *> steps;
    for (std::size_t k = from; k < inits.size(); k++) steps.push_back(inits[k].get());
    const std::unique_ptr<Var> self(Var::local(d.name, off));
    ConstObject value;
    if (!foldDeclaredObject(steps, *self, &value, d.pos)) refuseConstexprClassObject(d.name, d.pos);
    constObjects_[constObjectKey(*self)] = value;
}

Parser::Init Parser::parenthesisedInitialiser(const Declared &d) {
    expect("(");
    Init in;
    in.pos = peek().pos;
    // Read as call arguments are, so `T keep(std::forward<A>(a)...)` expands its pack: one value
    // initialises, none value-initialises ([temp.variadic]/7, [dcl.init]/11).
    std::vector<ExprPtr> values;
    parseArguments(values);
    if (values.size() > 1)
        src_.fail(in.pos, "'" + d.name + "' has type '" +
                          d.type->describe() + "', which takes one "
                          "initialiser - a list in parentheses is for a "
                          "class with a constructor that takes them");
    if (values.empty()) {
        in.value.reset(new Num(0LL));
        in.value->setType(types_.intType());
    } else {
        if (isBracedPlaceholder(*values[0]))
            src_.fail(in.pos, "a braced list in the parentheses that initialise '" + d.name +
                              "' is not supported yet");
        in.value = std::move(values[0]);
    }
    // [conv.bool]/1: `bool b(nullptr)` is false, a conversion direct-initialisation alone allows
    // (1.27, 2026-10-21, review of 1.25, item 3); `= nullptr` stays refused, as [dcl.init] has it.
    if (d.type->unqualified()->isBool() && in.value != nullptr && in.value->type() != nullptr &&
        in.value->type()->isNullPtr()) {
        in.value.reset(new Num(0LL));
        in.value->setType(types_.get(Kind::Bool));
    }
    // [over.match.conv]: direct-initialising a scalar from a class takes its
    // `explicit` conversions too (`bool c(f)`, not `bool b = f`), so the call
    // is made here, before the copy's rules see the value.
    if (!d.type->isReference() && !d.type->unqualified()->isStructOrUnion() && in.value != nullptr &&
        in.value->type() != nullptr && in.value->type()->unqualified()->isStructOrUnion()) {
        if (const Signature *how = conversionFunction(in.value->type(), d.type->unqualified(), true)) {
            const Type *object = in.value->type();
            std::vector<ExprPtr> none;
            in.value = memberCallWith(std::move(in.value), object, how->name, in.pos, std::move(none));
        }
    }
    return in;
}

// **Is the `(` ahead an initialiser or a parameter list?**
bool Parser::atParenInitialiser() {
    const std::size_t save = at_;
    at_++;                                    // the '('
    // **A parameter list begins with a decl-specifier-seq**, and that is the whole question.
    int wrap = 0;
    std::size_t nameAt = 0;
    const bool pack = (peek().kind == TokenKind::Ident && peekAt(1).is("...")) || packDeclarator(&wrap, &nameAt) != 0;
    // `auto` among them: a parameter declared with it is C++14 and is refused
    // by name where the parameter list is read, which only happens if this
    // says parameter list.
    bool parameters = peek().is(")") || peek().is("...") || pack ||
                      peek().is("static") || peek().is("register") ||
                      peek().is("auto") || atTypeName() ||
                      (peek().is("[") && peekAt(1).is("["));  // an attribute, never an expression
    // [dcl.ambig.res]/1: a declaration where it can be one - and `K g(K(42));` cannot: a type's name, a `(`
    // and a literal is no parameter, whose declarator would be a name. It is the object g, built from a
    // temporary (1.78: read as a function's declaration at namespace scope, "expected ')'").
    if (parameters && (peek().kind == TokenKind::Ident || peek().kind == TokenKind::Keyword) && peekAt(1).is("(") &&
        (peekAt(2).kind == TokenKind::Num || peekAt(2).kind == TokenKind::Str || peekAt(2).is("true") ||
         peekAt(2).is("false") || peekAt(2).is("nullptr")))
        parameters = false;
    at_ = save;
    return !parameters;
}

void Parser::checkOneDeducedType(const Type *&first, const Type *now,
                                 const std::string &name, std::size_t pos) {
    if (first == nullptr) { first = now; return; }
    if (first == now) return;
    src_.fail(pos, "'auto' deduces '" + now->describe() + "' for '" + name +
                   "' where the declarator before it in this declaration "
                   "deduced '" + first->describe() +
                   "' - [dcl.spec.auto] gives one 'auto' one type, however "
                   "many names are written after it. Split them into two "
                   "declarations");
}

StmtPtr Parser::declaration() {
    std::size_t pos = peek().pos;
    const ReadingAt reading(this, &stmtAt_);
    // `[[x]] n = 2;` reaches here, atDeclarationStart taking any `[[`: past them it is a statement.
    const bool attributed = peek().is("[") && peekAt(1).is("[");
    if (attributed && !attributedDeclarationAhead()) {
        StmtPtr s = statementBody();
        if (s) s->setPos(pos);
        return s;
    }
    StmtPtr s = declarationBody();
    if (s) s->setPos(pos);
    forgetUnsetArguments();
    return s;
}

StmtPtr Parser::declarationBody() {
    StorageClass sc;
    Qualifiers quals;
    const std::size_t specStart = at_;
    noreturnAt_ = 0;
    const Type *base = specifiers(&sc, &quals);
    // [dcl.stc]/4: a block-scope thread_local has static storage whether or not `static` is written.
    if ((quals.isThreadLocal || quals.threadLocalAsStatic) && sc == StorageNone) sc = StorageStatic;
    const ThreadLocalObject tlo(this, quals.isThreadLocal);

    if (peek().is(";")) {
        // [class.union]/5: a nameless union's members are locals sharing one slot; a nameless struct is refused.
        if (base->isStructOrUnion() && base->tag().empty()) {
            std::size_t key = specStart;
            while (key + 1 < at_ && !tokens_[key].is("union") && !tokens_[key].is("struct") && !tokens_[key].is("class")) key++;
            if (base->kind() != Kind::Union)
                src_.fail(tokens_[key].pos, "anonymous structs are a GNU extension");
            declareAnonymousUnionObject(base, sc, tokens_[key].pos, true);
        }
        at_++;
        return StmtPtr(new Block({}));
    }

    if (sc == StorageTypedef) {
        do {
            Declared td = declarator(base);
            refuseThreadLocalHere(quals.isThreadLocal || quals.threadLocalAsStatic, td.pos);
            typedefFunctionSuffix(td);
            // [dcl.typedef]/2: a typedef may redeclare the same type (C's
            // `typedef struct S S;`); only another type in this block is an
            // error - outer ones are hidden to the '}' ([basic.scope.block]).
            const std::map<std::string, const Type *>::const_iterator was = localTypes_.find(td.name);
            for (std::size_t u = localTypeBlockStart_; u < localTypeUndo_.size(); u++)
                if (localTypeUndo_[u].first == td.name && was != localTypes_.end() && was->second != td.type)
                    src_.fail(td.pos, "'" + td.name + "' is typedefed twice, "
                                      "and not to the same type: it was '" +
                                      was->second->describe() + "' and is now '" +
                                      td.type->describe() + "'");
            localTypeUndo_.push_back(std::make_pair(td.name, was != localTypes_.end() ? was->second : nullptr));
            localTypes_[td.name] = td.type;
        } while (consume(","));
        expect(";");
        return declarativeStatement();
    }

    if (sc == StorageExtern) {
        do {
            Declared d = declarator(base);
            if (peek().is("(")) { blockFunctionDeclaration(d); continue; }
            if (peek().is("="))
                src_.fail(d.pos, "'" + d.name + "' is extern, and an extern "
                                 "declaration cannot have an initialiser - the "
                                 "definition it names belongs at file scope");
            if (const GlobalSym *g = findGlobal(d.name))
                if (g->type != d.type)
                    src_.fail(d.pos, "'" + d.name + "' is declared '" +
                                     d.type->describe() + "' here and '" +
                                     g->type->describe() + "' at file scope");
            const GlobalSym *seen = findGlobal(d.name);
            declareStaticLocal(d.name, d.type, d.pos,
                               seen != nullptr ? seen->symbol
                                               : dataSymbol(d.name, d.type, false, d.pos));
            // `extern thread_local int t;` in a block: reached through its wrapper, as at file scope.
            if (seen != nullptr && seen->isThreadLocal != quals.isThreadLocal)
                src_.fail(d.pos, std::string(quals.isThreadLocal ? "thread-local" : "non-thread-local") +
                                 " declaration of '" + d.name + "' follows " +
                                 (quals.isThreadLocal ? "non-thread-local" : "thread-local") + " declaration");
            locals_.back().isThreadLocal = quals.isThreadLocal;
            locals_.back().tlsViaWrapper = quals.isThreadLocal;
            if (quals.isThreadLocal) fnVars_.pop_back();
        } while (consume(","));
        expect(";");
        return StmtPtr(new Block({}));
    }

    std::vector<StmtPtr> inits;
    // What the first declarator of an `auto` declaration deduced - see
    // checkOneDeducedType. Null while there has been none.
    const Type *deducedSoFar = nullptr;
    do {
        Declared d = declarator(base);
        d.type = constexprObjectType(d.type, quals);
        // **A condition declares one name and must initialise it**, and both
        // are settled here: the loop below leaves by more than one path, so
        // asking at the end would mean asking in several places.
        if (conditionDecl_) {
            conditionName_ = d.name;
            if (peek().is(")"))
                src_.fail(d.pos, "'" + d.name + "' is declared in a condition "
                                 "and has no initialiser - there would be "
                                 "nothing to test");
        }
        // `auto f(int) -> int;` in a block declares a function, its return type after the list.
        if (mentionsDeduced(d.type) && trailingArrowAhead()) {
            refuseTrailingBase(d.type, d.pos);
            std::vector<const Type *> params;
            bool variadic = false;
            const std::size_t open = at_;
            parameterTypes(params, variadic);
            pendingNoexcept_ = exceptionSpecification();
            const Type *returns = readTrailingReturn(open);
            declareFunction(d.name, returns, params, variadic, false, d.pos);
            continue;
        }
        if (mentionsDeduced(d.type)) {
            d.type = deduceAuto(d.type, d.name, d.pos);
            checkOneDeducedType(deducedSoFar, lastDeducedAuto_, d.name, d.pos);
        }

        // **A const object has to be initialised where it is declared**, and
        // this is asked before the branch below rather than after it.
        if (d.type->isConst() && !peek().is("=") && !peek().is("(") &&
            !peek().is("{"))
            requireConstInitialised(d.type, d.name, d.pos);
        if (sc != StorageExtern) refuseInaccessibleDestructor(d.type, d.pos, "'" + d.name + "'");
        refuseNoreturnObject(d.name);

        // An object of a class with constructors is built by calling one, asked before
        // the branch below - `Point p(1)` and a function declaration look alike until
        // the type is known. **And an array of one**, which used to fall through.
        {
            const Type *elem = d.type;
            while (elem != nullptr && elem->isArray()) elem = elem->pointee();
            const Type *plain = elem == nullptr ? nullptr : elem->unqualified();
            const bool arrayCtor = d.type->isArray() && plain != nullptr &&
                plain->isStructOrUnion() && !plain->tag().empty() &&
                hasConstructors(plain->tag());
            const bool arrayDtor = d.type->isArray() && plain != nullptr &&
                plain->isStructOrUnion() && !plain->tag().empty() &&
                destructorOf(plain) != nullptr;
            // An array of aggregates from a list is filled member by member, [dcl.init.aggr].
            const bool aggregates = arrayCtor && !arrayDtor && isAggregateClass(plain) && bracedInitialiserAhead();
            if ((arrayCtor || arrayDtor) && !aggregates) {
                // [stmt.dcl]/4: a static one is built once, under a guard,
                // and its elements destroyed at exit - as one object is.
                if (sc == StorageStatic) {
                    staticLocalWithConstructor(d, inits);
                    continue;
                }
                // [dcl.init]/17: an array takes a braced list, `= { ... }` or `{ ... }`.
                if (peek().is("("))
                    src_.fail(d.pos, "an array is initialised from a braced list - "
                                     "write '" + d.name + "[...] = { ... }'");
                const bool listed = peek().is("{") || (peek().is("=") && peekAt(1).is("{"));
                if (peek().is("=") && !listed)
                    src_.fail(d.pos, "an array of '" + plain->describe() + "' is "
                                     "initialised from a braced list, '= { ... }'");
                if (listed && !arrayCtor)
                    src_.fail(d.pos, "a braced list for an array of '" + plain->describe() +
                                     "', a class with a destructor and no constructor, is "
                                     "not supported yet");
                if (d.type->length() < 0 && !listed)
                    src_.fail(d.pos, "'" + d.name + "' has no length and no initialiser "
                                     "to take one from");
                const auto declareIt = [&]() {
                    const int at = declare(d.name, d.type, d.pos, quals.alignAs);
                    locals_.back().guardsJump = true;
                    return at;
                };
                // An array of no length is declared once its list has given it one.
                const bool late = d.type->length() < 0;
                int off = late ? 0 : declareIt();
                if (listed) {
                    consume("=");
                    StmtPtr built = constructLocalArrayFromList(d, off, declareIt);
                    if (late) off = findLocal(d.name)->offset;
                    inits.push_back(std::move(built));
                } else if (arrayCtor) {
                    int indexSlot = allocateFrameSlot(types_.intType());
                    inits.push_back(constructLocalArray(d, off, indexSlot));
                }
                // **Destroyed last first when the scope ends** - [class.dtor]
                // - as one entry carrying the count, by the class's loop.
                if (destructorOf(plain) != nullptr) {
                    long long count = 1;
                    for (const Type *t = d.type; t->isArray(); t = t->pointee())
                        count *= t->length();
                    Alive whole{ d.name, off, plain };
                    whole.count = count;
                    alive_.push_back(whole);
                }
                // **The comma belongs to the loop condition.**
                continue;
            }
        }

        // A static one of a class with no constructor and a destructor is built by nothing and still destroyed
        // at exit: without an initialiser it goes the same way, for the registration alone.
        const bool staticDestroyedOnly = sc == StorageStatic && d.type->isStructOrUnion() &&
                                         !d.type->tag().empty() && !hasConstructors(d.type->tag()) &&
                                         destructorOf(d.type->unqualified()) != nullptr &&
                                         !peek().is("=") && !peek().is("(") && !peek().is("{");
        if (staticDestroyedOnly ||
            (d.type->isStructOrUnion() && !d.type->tag().empty() &&
             hasConstructors(d.type->tag()) && !(isAggregateClass(d.type) && bracedInitialiserAhead()))) {
            // [stmt.dcl]/4: a static one is built the first time control
            // passes through, under a guard, and destroyed at exit.
            if (sc == StorageStatic) {
                staticLocalWithConstructor(d, inits);
                continue;
            }
            const bool early = initialiserNames(d.name);
            // **The named return value** (1.81): every `return` in this object's scope returns it, so it is the
            // caller's object from the start - its slot holds the caller's address, as a by-value parameter's
            // does, and `return` builds nothing. cl and clang leave the move out here, and until 1.81 cpp11
            // built the object in its own frame and moved its bytes to the caller's.
            const bool named = !early && quals.alignAs == 0 && !quals.isConstexpr && sretSlot_ != 0 &&
                               !namedReturnInScope() && namedReturnCandidate(d.name, d.type);
            int off = early ? declare(d.name, d.type, d.pos, quals.alignAs) : 0;
            const std::size_t mine = locals_.size() - 1;
            CtorInit ci = readConstructorInitialiser(d);
            if (!early) off = declare(d.name, named ? types_.referenceTo(d.type) : d.type, d.pos, quals.alignAs);
            locals_[early ? mine : locals_.size() - 1].guardsJump = true;
            if (named) {
                ExprPtr held(Var::local(d.name, off));
                held->setType(types_.pointerTo(d.type->unqualified()));
                ExprPtr caller(Var::local(".sret", sretSlot_));
                caller->setType(types_.pointerTo(d.type->unqualified()));
                ExprPtr point(new Assign(std::move(held), std::move(caller)));
                point->setType(types_.pointerTo(d.type->unqualified()));
                inits.push_back(StmtPtr(new ExprStmt(std::move(point))));
                nrvoSlot_ = off;
                nrvoName_ = d.name;
                for (std::size_t z = 0; z < ci.ilSetup.size(); z++)
                    inits.push_back(std::move(ci.ilSetup[z]));
                inits.push_back(constructLocal(d, off, std::move(ci.args), ci.copyInit, ci.valueInit));
                const Alive whole{ d.name, off, d.type->unqualified(), true };
                flushOverObject(inits, whole, d.pos);
                if (destructorOf(d.type) != nullptr) alive_.push_back(whole);
                continue;  // the comma belongs to the loop condition
            }

            // The backing array and the list object, before the constructor
            // that reads them - a list-init only.
            for (std::size_t z = 0; z < ci.ilSetup.size(); z++)
                inits.push_back(std::move(ci.ilSetup[z]));
            const std::size_t builtFrom = inits.size();

            // **Copy elision, in the one case worth having it**: where the initialiser is a call
            // already returning through a hidden pointer, the object is built straight into this
            // variable. clang does it at -O0, cl does not; both may.
            Call *made = ci.args.size() == 1 &&
                         (d.type->nonTrivialCopy() ||
                          destructorOf(d.type) != nullptr)
                       ? resultCall(ci.args[0].get()) : nullptr;

            // **And where it is a temporary of the class itself**, `K h = K(5);` and `K h(K(5));`: the temporary
            // is the variable - its slot is the variable's, no copy or move constructor is called, and what
            // would have destroyed the temporary at the end of the statement does not (1.78; cl and clang
            // leave the copy out too, [class.copy]/31).
            const int isTemporary = !early && quals.alignAs == 0 && ci.args.size() == 1
                                  ? freshTemporarySlot(*ci.args[0], d.type) : 0;

            if (made != nullptr && made->type() == d.type &&
                returnsIndirectly(d.type, made->hasThis())) {
                claimCallResult(*made, off);
                inits.push_back(StmtPtr(new ExprStmt(std::move(ci.args[0]))));
            } else if (isTemporary != 0) {
                adoptTemporary(isTemporary);
                locals_.back().offset = isTemporary;
                off = isTemporary;
                inits.push_back(StmtPtr(new ExprStmt(std::move(ci.args[0]))));
            } else if (ci.trivialCopy) {
                ExprPtr target(Var::local(d.name, off));
                target->setType(d.type);
                ExprPtr store(new Assign(std::move(target), std::move(ci.args[0])));
                store->setType(d.type);
                inits.push_back(StmtPtr(new ExprStmt(std::move(store))));
            } else {
                inits.push_back(constructLocal(d, off, std::move(ci.args),
                                               ci.copyInit, ci.valueInit));
            }
            if (quals.isConstexpr) foldLocalObject(d, off, inits, builtFrom);
            flushOverObject(inits, Alive{ d.name, off, d.type->unqualified() }, d.pos);
            if (destructorOf(d.type) != nullptr)
                alive_.push_back(Alive{ d.name, off, d.type->unqualified() });
            continue;  // the comma belongs to the loop condition
        }

        // **`X q(p);` where X has no constructor at all.** Its copy is trivial, so what
        // the standard asks for is the bytes - the struct assignment the backends
        // already emit. A parameter list begins with a type name and this does not.
        if (peek().is("(") && d.type->isStructOrUnion() && sc != StorageStatic) {
            if (atParenInitialiser()) {
                const bool early = initialiserNames(d.name);
                int off = early ? declare(d.name, d.type, d.pos, quals.alignAs) : 0;
                const std::size_t mine = locals_.size() - 1;
                at_++;                        // the '('
                std::vector<ExprPtr> args;
                parseArguments(args);
                if (args.size() != 1)
                    src_.fail(d.pos, "'" + d.type->describe() + "' has no "
                                     "constructor, so '" + d.name + "(...)' can "
                                     "only be a copy of another '" +
                                     d.type->describe() + "' - and this gives " +
                                     std::to_string(args.size()) + " arguments");
                checkAssignable(*args[0], d.type, d.pos, "'" + d.name + "'");
                if (!early) off = declare(d.name, d.type, d.pos, quals.alignAs);
                locals_[early ? mine : locals_.size() - 1].guardsJump = true;
                ExprPtr target(Var::local(d.name, off));
                target->setType(d.type);
                ExprPtr store(new Assign(std::move(target), std::move(args[0])));
                store->setType(d.type);
                inits.push_back(StmtPtr(new ExprStmt(std::move(store))));
                if (destructorOf(d.type) != nullptr)
                    alive_.push_back(Alive{ d.name, off, d.type->unqualified() });
                // **The comma belongs to the loop condition.**
                continue;
            }
        }

        // **`int z(5);` is direct-initialisation, not a function.**
        const bool parenInit = peek().is("(") && !d.type->isStructOrUnion() &&
                               !d.type->isArray() && !d.type->isReference() &&
                               atParenInitialiser();
        // **`int &r(v);` is direct-initialisation of a reference**, [dcl.init]/16 - also what `T t(e);` is in
        // a template whose T is a reference (1.79: it was read as a function's declaration, "expected a type").
        const bool refParen = peek().is("(") && d.type->isReference() && atParenInitialiser();
        // **Not in a condition**, which takes `= expr` or braces and nothing
        // else - [stmt.select]/1 spells the grammar out, and clang says so by
        // name.
        if (parenInit && conditionDecl_)
            src_.fail(peek().pos, "a declaration in a condition is initialised "
                                  "with '=' or with braces, not with "
                                  "parentheses - [stmt.select] gives the "
                                  "condition its own grammar, and this one "
                                  "would read like a call");

        if (peek().is("(") && !parenInit && !refParen) {
            if (sc == StorageStatic)
                src_.fail(d.pos, "'" + d.name + "' is a function declared inside a "
                                 "block, and such a declaration is always extern - "
                                 "drop the 'static' or move it to file scope");
            blockFunctionDeclaration(d);
            continue;
        }
        if (d.type->isReference()) {
            // [stmt.dcl]/4 again: bound once, statically where the initialiser
            // is a global's address and under a guard otherwise.
            if (sc == StorageStatic) {
                const std::string symbol = uniqueStaticSymbol(d.name);
                std::vector<GlobalPiece> pieces;
                bool hasInit = false;
                std::vector<StmtPtr> body;
                const bool early = initialiserNames(d.name);
                if (early) declareStaticLocal(d.name, d.type, d.pos, symbol);
                const std::size_t mine = locals_.size() - 1;
                bindStaticReference(d, symbol, pieces, hasInit, &body);
                if (!early) declareStaticLocal(d.name, d.type, d.pos, symbol);
                markThreadLocal(early ? mine : locals_.size() - 1, quals.isThreadLocal);
                current_->globals.push_back(Global{ symbol, symbol,
                                                    types_.pointerTo(d.type->referent()),
                                                    std::move(pieces), hasInit,
                                                    true, false });
                current_->globals.back().isThreadLocal = quals.isThreadLocal;
                if (!body.empty())
                    inits.push_back(guardOnce(symbol, std::move(body)));
                continue;
            }
            if (!peek().is("=") && !peek().is("{") && !refParen)
                src_.fail(d.pos, "'" + d.name + "' is a reference and has to be "
                                 "initialised here - there is no later "
                                 "assignment that would bind it, only one that "
                                 "writes through it");
            const bool early = initialiserNames(d.name);
            int off = early ? declare(d.name, d.type, d.pos, quals.alignAs) : 0;
            const std::size_t mine = locals_.size() - 1;
            ExprPtr init;
            const Type *listArray = d.type->referent()->isArray() &&
                                    (peek().is("{") || (peek().is("=") && peekAt(1).is("{")))
                                    ? d.type->referent() : nullptr;
            if (listArray != nullptr) {
                // **[dcl.init.list]/3 with [dcl.init.ref]/5: `int (&&ra)[3] = {1, 2, 3};` initialises a
                // temporary array from the list and binds the reference to it**, for as long as the reference
                // lives ([class.temporary]/5) - a local of its own, under a name no source can spell (R22).
                const std::size_t bpos = peek().pos;
                if (!d.type->isRValueReference() && (!listArray->isConst() || listArray->isVolatile()))
                    src_.fail(bpos, "'" + d.name + "' is '" + d.type->describe() + "', and a braced list makes a "
                                    "temporary array - [dcl.init.ref]/5 binds one to an rvalue reference or a "
                                    "reference to const alone");
                if (listArray->length() < 0)
                    src_.fail(bpos, "'" + d.name + "' refers to an array of unknown bound, and C++11 gives a "
                                    "braced list no array type to make for it - write the bound");
                consume("=");
                const std::string held = d.name + "$list";
                const int heldAt = declare(held, listArray, d.pos);
                Init in = parseInitialiser();
                std::vector<InitStep> path;
                emitInit(held, path, listArray, in, inits);
                if (destructorOf(listArray->innermostElement()) != nullptr) {
                    long long count = 1;
                    for (const Type *t = listArray; t->isArray(); t = t->pointee()) count *= t->length();
                    Alive whole{ held, heldAt, listArray->innermostElement()->unqualified() };
                    whole.count = count;
                    alive_.push_back(whole);
                }
                ExprPtr v(Var::local(held, heldAt));
                v->setType(listArray);
                v->setXvalue();                 // the temporary an rvalue reference takes
                init = std::move(v);
            } else if ((peek().is("{") || (peek().is("=") && peekAt(1).is("{"))) &&
                       d.type->referent()->unqualified()->isStructOrUnion()) {
                // [dcl.init.list]/3: `const auto &r = {5, 6};`, `const S &s{1, 2}` - a temporary of the class the
                // reference names, copy-list-initialised, bound and kept for as long as the reference.
                consume("=");
                const std::size_t bpos = peek().pos;
                const Type *to = d.type->referent();
                if (!d.type->isRValueReference() && (!to->isConst() || to->isVolatile()))
                    src_.fail(bpos, "'" + d.name + "' is '" + d.type->describe() + "', and a braced list makes a "
                                    "temporary - [dcl.init.ref]/5 binds one to an rvalue reference or a "
                                    "reference to const alone");
                Init in = parseInitialiser();
                init = isInitializerListType(to) ? buildInitializerList(to->unqualified(), in, bpos, inits)
                                                 : listTemporaryFrom(to, in, true, bpos);
                init->setXvalue();
            } else if (peek().is("{") || (peek().is("=") && peekAt(1).is("{"))) {
                // [dcl.init.list]/3: `const int &r{9};` binds the reference to the list's one element
                // (1.27, 2026-10-21, review of 1.25, item 3: only `=` was read here) - and `const int &r = {9};`
                // is the same initialisation with its `=` (1.80: "expected an expression").
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
                if (refParen) expect(")");
            }
            // [expr.const]/2-3 with [dcl.constexpr]/9: a constexpr reference binds an object of static
            // storage duration, and a temporary in a block has none.
            if (quals.isConstexpr && sc != StorageStatic && !isLvalue(*init) &&
                dynamic_cast<const StrLit *>(init.get()) == nullptr)
                src_.fail(d.pos, "'" + d.name + "' is a 'constexpr' reference, and it would bind a temporary - a "
                                 "constant expression refers only to an object with static storage duration");
            if (!early) off = declare(d.name, d.type, d.pos, quals.alignAs);
            locals_[early ? mine : locals_.size() - 1].guardsJump = true;
            const Type *slot = types_.pointerTo(d.type->referent());
            ExprPtr addr = bindReference(d.type, std::move(init), d.pos,
                                         "'" + d.name + "'");
            ExprPtr target(Var::local(d.name, off));
            target->setType(slot);
            // **The temporary this reference binds to, if it made one**, is
            // extended to the scope rather than destroyed at the semicolon -
            // [class.temporary]/5.
            extendTemporary(*addr, d.name + "$held");
            ExprPtr bind(new Assign(std::move(target), std::move(addr)));
            bind->setType(slot);
            inits.push_back(StmtPtr(new ExprStmt(std::move(bind))));
            // And what else the initialiser built (T(1) in `const T &r =
            // f(T(1));`) dies here, [class.temporary]/5's second exception -
            // flushed whether or not something was extended.
            flushTemporaries(inits, true);
            continue;
        }

        // `int arr[] = {1, 2, 3}` and, [dcl.init.list], `int arr[]{1, 2, 3}` (1.27, 2026-10-21, review of
        // 1.25, item 3: the braces without the '=' left the array "incomplete").
        bool sizedByInitialiser = d.type->isArray() && d.type->length() < 0 &&
                                  (peek().is("=") || atBracedInitialiser(d.name));
        if (!d.type->isComplete() && !sizedByInitialiser) realizeMemberClass(d.type, true);
        if (!d.type->isComplete() && !sizedByInitialiser)
            src_.fail(d.pos, "'" + d.name + "' has an incomplete type");
        if (sc != StorageExtern && d.type->isArray() && !d.type->innermostElement()->isComplete())
            src_.fail(d.pos, "'" + d.name + "' is an array of the incomplete type '" +
                             d.type->innermostElement()->describe() + "'");
        checkNotAbstract(d.type, d.pos, "'" + d.name + "'");

        if (sc == StorageStatic) {
            const std::string symbol = uniqueStaticSymbol(d.name, quals.isThreadLocal ? nullptr : d.type);
            std::vector<GlobalPiece> pieces;
            bool hasInit = false;
            // In scope in its own initialiser, as an automatic one is.
            const bool early = d.type->isComplete() && initialiserNames(d.name);
            if (early) declareStaticLocal(d.name, d.type, d.pos, symbol);
            const std::size_t mine = locals_.size() - 1;
            Init dynamic;
            if (parenInit || consume("=") || atBracedInitialiser(d.name)) {
                OverloadTarget hold(*this, d.type);
                Init in = parenInit ? parenthesisedInitialiser(d)
                                    : parseInitialiser();
                if (d.type->isArray() && d.type->length() < 0)
                    d.type = types_.arrayOf(d.type->pointee(),
                                            inferredLength(in, d.type->pointee(), d.pos));
                // [stmt.dcl]/4: one that is not a constant expression is initialised the
                // first time control passes, under the guard a constructor gets; a constant
                // stays data, and `constexpr` keeps its demand for one.
                if (quals.isConstexpr || staticallyInitialisable(d.type, in)) {
                    flattenInit(d.type, in, 0, pieces);
                    hasInit = true;
                } else {
                    dynamic = std::move(in);
                }
            } else if (d.type->isArray() && d.type->length() < 0) {
                src_.fail(d.pos, "'" + d.name + "' has no length and no initialiser "
                                 "to take one from");
            }
            const std::size_t at = early ? mine : locals_.size();
            if (!early) declareStaticLocal(d.name, d.type, d.pos, symbol);
            locals_[at].isConst = d.type->isConst();
            markThreadLocal(at, quals.isThreadLocal);
            // Not `isConst` where the guarded store writes it: it cannot live in .rodata.
            // An inline function's static local is one object for the program: external, in a
            // COMDAT, under the ABI's name (1.28, 2026-10-22, review of 1.25, item 14).
            current_->globals.push_back(Global{ symbol, symbol, d.type,
                                                std::move(pieces), hasInit,
                                                !sharedStaticLocal(symbol),
                                                locals_[at].isConst &&
                                                    dynamic.value == nullptr });
            current_->globals.back().isInline = sharedStaticLocal(symbol);
            current_->globals.back().align = quals.alignAs;
            current_->globals.back().isThreadLocal = quals.isThreadLocal;
            if (dynamic.value != nullptr) {
                std::vector<StmtPtr> body;
                std::vector<InitStep> path;
                emitInit(d.name, path, d.type, dynamic, body);
                flushTemporaries(body);
                inits.push_back(guardOnce(symbol, std::move(body)));
            }
            continue;
        }

        bool hasInit = parenInit || peek().is("=") || atBracedInitialiser(d.name);
        // **[basic.scope.pdecl]/1: the name is declared before its initialiser**,
        // so `void *q = &q;` takes its own address - and an outer `q` is hidden.
        const bool early = hasInit && d.type->isComplete() && initialiserNames(d.name);
        int off = early ? declare(d.name, d.type, d.pos, quals.alignAs) : 0;
        std::size_t mine = locals_.size() - 1;
        Init in;
        if (hasInit) {
            OverloadTarget hold(*this, d.type);
            if (parenInit) {
                in = parenthesisedInitialiser(d);
            } else {
            consume("=");
            in = parseInitialiser();
            if (d.type->isArray() && d.type->length() < 0)
                d.type = types_.arrayOf(d.type->pointee(),
                                        inferredLength(in, d.type->pointee(), d.pos));
            }
        } else if (d.type->isArray() && d.type->length() < 0) {
            src_.fail(d.pos, "'" + d.name + "' has no length and no initialiser "
                             "to take one from");
        }

        if (!hasInit) refuseDeletedDefaultInit(d.type, d.name, d.pos);
        if (!early) {
            off = declare(d.name, d.type, d.pos, quals.alignAs);
            mine = locals_.size() - 1;
        }
        locals_[mine].isConst = d.type->isConst();
        locals_[mine].isRegister = (sc == StorageRegister);
        // An initialiser to skip, or a destructor that would run on what was
        // never built: either makes this a declaration no jump may land past.
        locals_[mine].guardsJump = hasInit || destructorOf(d.type) != nullptr;
        locals_[mine].unset = !hasInit && sc != StorageStatic && sc != StorageExtern && readingProgram() &&
                              (d.type->isArithmetic() || d.type->isPointer());
        if (hasInit) {
            long long value = 0;
            long double dvalue = 0;
            ConstObject constantArray;
            if (constantInitialiser(d.type, in, &value)) {
                locals_[mine].isConstantValue = true;
                locals_[mine].constantValue = value;
            } else if (constantFloatingInitialiser(d.type, in, &dvalue)) {
                // A const floating local reads back the same way a global does.
                locals_[mine].isConstantDouble = true;
                locals_[mine].constantDouble = dvalue;
            } else if (quals.isConstexpr && d.type->innermostElement()->unqualified()->isStructOrUnion()) {
                if (d.type->isArray()) refuseConstexprClassObject(d.name, d.pos);
            } else if (quals.isConstexpr && d.type->isArray() &&
                       (d.type->innermostElement()->isArithmetic() || d.type->innermostElement()->isPointer()) &&
                       constObjectFromInit(d.type, in, 0, &constantArray)) {
                // [dcl.constexpr]/9: an array of numbers or pointers, read as a constant object's leaves are.
                const std::unique_ptr<Var> self(Var::local(d.name, off));
                constObjects_[constObjectKey(*self)] = constantArray;
            } else if (quals.isConstexpr && d.type->isPointer() &&
                       constantPointerInit(in, currentFunction_ + "@" + std::to_string(off))) {
            } else if (quals.isConstexpr) {
                src_.fail(d.pos, "'" + d.name + "' is 'constexpr', so its value "
                                 "has to be known while this is compiled, and "
                                 "this initialiser is not a constant expression");
            }
        } else if (quals.isConstexpr) {
            src_.fail(d.pos, "'" + d.name + "' is 'constexpr' and has no "
                             "initialiser - there is nothing for it to be");
        }

        // **An object with a destructor is alive from here**, whether or not it had a
        // constructor to run. Before implicit destructors existed only the constructor
        // path added to this list, so such a class was destroyed by nobody.
        if (destructorOf(d.type) != nullptr)
            alive_.push_back(Alive{ d.name, off, d.type->unqualified() });

        if (hasInit) {
            std::vector<InitStep> path;
            const std::size_t built = inits.size();
            emitInit(d.name, path, d.type, in, inits);
            if (quals.isConstexpr && d.type->unqualified()->isStructOrUnion()) foldLocalObject(d, off, inits, built);
        }
        flushTemporaries(inits, true);
    } while (!conditionDecl_ && consume(","));

    // A condition ends at the caller's `)` rather than at a `;`, and it
    // declares one name - [stmt.select]/2 allows a single declarator.
    if (conditionDecl_) {
        if (peek().is(","))
            src_.fail(peek().pos, "a condition declares one name - "
                                  "[stmt.select] allows a single declarator "
                                  "here");
        return StmtPtr(new Block(std::move(inits)));
    }
    expect(";");
    return StmtPtr(new Block(std::move(inits)));
}

// **A declaration followed by `:` rather than `;`.** Telling that from
// `for (int x = a ? b : c; ...)` is the whole difficulty: a `?` claims the next `:`,
// so they are counted. `::` is one token from the lexer and cannot be mistaken.
bool Parser::atRangeFor() const {
    int depth = 0;
    int question = 0;
    for (std::size_t k = 0; ; k++) {
        const Token &t = peekAt(k);
        if (t.kind == TokenKind::End) return false;
        if (t.is("(") || t.is("[")) { depth++; continue; }
        if (t.is(")") || t.is("]")) {
            if (depth == 0) return false;
            depth--;
            continue;
        }
        if (depth != 0) continue;
        if (t.is(";")) return false;
        if (t.is("?")) { question++; continue; }
        if (t.is(":")) {
            if (question > 0) { question--; continue; }
            return true;
        }
    }
}

// **[stmt.ranged] is a rewrite, and this does the rewrite.** The standard says
// what `for (T x : a)` means by writing another loop, and every node that loop
// needs was already here.
const Type *Parser::classRangeEnds(ExprPtr range, std::size_t rpos,
                                   std::vector<StmtPtr> &setup,
                                   int *bSlot, int *eSlot,
                                   std::string *bName, std::string *eName) {
    const Type *rangeType = range->type();
    const Type *cls = rangeType->unqualified();

    // A temporary range is what `auto &&__range` binds and keeps alive to the loop's end,
    // [class.temporary]/5: here it is built into a slot of its own, destroyed after the loop.
    if (isGlvalue(*range)) {
        extendTemporary(*range, ".rt" + std::to_string(refTemps_++));
    } else {
        const std::string tName = ".rt" + std::to_string(refTemps_++);
        const int tSlot = declare(tName, cls, rpos);
        setup.push_back(initRangeLocal(tName, cls, tSlot, std::move(range), rpos));
        rangeBuilt_.push_back(std::make_pair(setup.size(), alive_.size()));
        range.reset(Var::local(tName, tSlot));
        range->setType(cls);
        rangeType = cls;
    }

    // [stmt.ranged]/1: members if the class has either, else `begin(r)` and `end(r)`
    // found by argument-dependent lookup.
    const bool members = findMemberOwner(cls, "begin") != nullptr ||
                         findMemberOwner(cls, "end") != nullptr;
    // Lookup by the argument alone: the class's own namespace, or the global one - read before a
    // specialization's argument list, whose own `std::` is no part of it.
    const std::string::size_type angle = cls->tag().find('<');
    const std::string::size_type cut = cls->tag().rfind("::", angle);
    const std::string scope = cls->inNamespace() && cut != std::string::npos
                            ? cls->tag().substr(0, cut + 2) : std::string();
    // That namespace's function templates are candidates beside its functions: std::begin(valarray<T> &).
    if (!members) {
        std::vector<const Type *> argTypes(1, rangeType);
        for (const char *which : { "begin", "end" }) {
            std::string named;
            const std::vector<TemplateDecl> reached =
                templatesNamed(which, scope.empty() ? std::string() : scope.substr(0, scope.size() - 2), &named);
            if (!reached.empty()) instantiateViableTemplates(reached, argTypes, rpos);
        }
    }
    if (!members && (!hasFunctionNamed(scope + "begin") || !hasFunctionNamed(scope + "end")))
        src_.fail(rpos, "'" + cls->describe() + "' has no 'begin' and 'end', as "
                        "members or as functions found by argument-dependent lookup, "
                        "so a range-based 'for' has nothing to walk");

    // `R *__r = &range;`
    const Type *rangePtr = types_.pointerTo(rangeType);
    const std::string rName = ".rr" + std::to_string(refTemps_++);
    const int rSlot = declare(rName, rangePtr, rpos);
    ExprPtr held(Var::local(rName, rSlot));
    held->setType(rangePtr);
    ExprPtr addr(new Unary('&', std::move(range)));
    addr->setType(rangePtr);
    ExprPtr keep(new Assign(std::move(held), std::move(addr)));
    keep->setType(rangePtr);
    setup.push_back(StmtPtr(new ExprStmt(std::move(keep))));
    rangeBuilt_.push_back(std::make_pair(setup.size(), alive_.size()));

    // `*__r`, rebuilt for each call: an ExprPtr is used up by the one that takes it.
    auto object = [&]() {
        ExprPtr p(Var::local(rName, rSlot));
        p->setType(rangePtr);
        ExprPtr o(new Unary('*', std::move(p)));
        o->setType(rangeType);
        return o;
    };
    auto end = [&](const char *which) -> ExprPtr {
        if (members)
            return memberCallWith(object(), rangeType, which, rpos, std::vector<ExprPtr>());
        std::vector<ExprPtr> args;
        args.push_back(object());
        const Signature sig = resolveOverload(scope + which, args, rpos);
        return completeCall(scope + which, sig.symbol, nullptr, sig.returns, sig.params,
                            sig.variadic, rpos, std::move(args), false);
    };

    ExprPtr first = end("begin");
    const Type *iter = first->type()->unqualified();
    if (!iter->isPointer() && !iter->isStructOrUnion())
        src_.fail(rpos, "'begin' of '" + cls->describe() + "' returns '" +
                        iter->describe() + "', which is neither a pointer nor a "
                        "class an iterator can be");
    *bName = ".rb" + std::to_string(refTemps_++);
    *bSlot = declare(*bName, iter, rpos);
    setup.push_back(initRangeLocal(*bName, iter, *bSlot, std::move(first), rpos));
    rangeBuilt_.push_back(std::make_pair(setup.size(), alive_.size()));

    ExprPtr last = end("end");
    if (last->type()->unqualified() != iter)
        src_.fail(rpos, "'begin' and 'end' of '" + cls->describe() + "' return "
                        "different types, '" + iter->describe() + "' and '" +
                        last->type()->describe() + "' - C++11's range-based 'for' "
                        "wants one type for both; C++17 relaxed that");
    *eName = ".re" + std::to_string(refTemps_++);
    *eSlot = declare(*eName, iter, rpos);
    setup.push_back(initRangeLocal(*eName, iter, *eSlot, std::move(last), rpos));
    rangeBuilt_.push_back(std::make_pair(setup.size(), alive_.size()));
    return iter;
}

// One of the range-for's own variables initialised from `init`: a pointer stored, a class
// built as `T v = init;` would be - by the call itself where it returns through a slot -
// and alive from here so the loop's end destroys it, and so does a `return` inside.
StmtPtr Parser::initRangeLocal(const std::string &name, const Type *t, int off,
                               ExprPtr init, std::size_t pos) {
    std::vector<StmtPtr> out;
    Call *made = resultCall(init.get());
    const bool owns = t->isStructOrUnion() &&
                      (t->nonTrivialCopy() || destructorOf(t) != nullptr);
    if (made != nullptr && owns && made->type()->unqualified() == t &&
        returnsIndirectly(t, made->hasThis())) {
        claimCallResult(*made, off);
        out.push_back(StmtPtr(new ExprStmt(std::move(init))));
    } else if (owns && hasConstructors(t->tag())) {
        std::vector<ExprPtr> args;
        args.push_back(std::move(init));
        out.push_back(constructLocal(Declared{ name, t, pos, 0, std::string() }, off,
                                     std::move(args), true));
    } else {
        ExprPtr target(Var::local(name, off));
        target->setType(t);
        ExprPtr store(new Assign(std::move(target), convert(std::move(init), t)));
        store->setType(t);
        out.push_back(StmtPtr(new ExprStmt(std::move(store))));
    }
    flushTemporaries(out);
    if (t->isStructOrUnion() && destructorOf(t) != nullptr)
        alive_.push_back(Alive{ name, off, t });
    return StmtPtr(new Block(std::move(out)));
}

StmtPtr Parser::rangeForStatement(int scope) {
    StorageClass sc;
    Qualifiers quals;
    const Type *base = specifiers(&sc, &quals);
    Declared d = declarator(base);
    expect(":");

    const std::size_t rpos = peek().pos;
    // **A braced list as the range is an `initializer_list`** - [stmt.ranged] binds the range
    // to `auto &&`, which for braces deduces one; it is then walked as any class range is (F27d).
    const std::size_t aliveBefore = alive_.size();
    std::vector<StmtPtr> listSetup;
    ExprPtr range;
    if (peek().is("{")) {
        const std::size_t braces = at_;
        const Type *il = deduceBracedAuto("the range", rpos);
        at_ = braces;
        Init in = parseInitialiser();
        range = buildInitializerList(il, in, rpos, listSetup);
    } else {
        range = expr();
    }
    expect(")");

    const Type *rt = range->type();
    // `auto &&` deduces `T &` from the lvalue `*__b`; a `T &&` written out cannot bind one.
    if (d.type->kind() == Kind::RValueRef && !mentionsDeduced(d.type))
        src_.fail(d.pos, "'" + d.name + "' is '" + d.type->describe() + "', and each element "
                         "is an lvalue - an rvalue reference cannot bind to one");

    // **The two ends of the loop, and the only thing the two kinds of range
    // disagree about.**
    const Type *elemPtr = nullptr;
    std::vector<StmtPtr> setup = std::move(listSetup);  // a braced range's backing array and list
    int bSlot = 0, eSlot = 0;
    std::string bName, eName;
    const std::size_t aliveMark = aliveBefore;
    rangeBuilt_.clear();

    if (rt->unqualified()->isStructOrUnion()) {
        elemPtr = classRangeEnds(std::move(range), rpos, setup,
                                 &bSlot, &eSlot, &bName, &eName);
    } else {
        if (!rt->isArray())
            src_.fail(rpos, "a range-based 'for' needs an array or a class "
                            "with 'begin' and 'end', and this is '" +
                            rt->describe() + "'");
        if (rt->length() < 0)
            src_.fail(rpos, "this array has no length, so there is nothing to "
                            "stop at");

        elemPtr = types_.pointerTo(rt->pointee());

        // `for (x : H().a)`: H() lives as long as __range, [class.temporary]/5
        const bool kept = extendTemporary(*range, ".rt" + std::to_string(refTemps_++));

        // `T *__b = a;` - the array decayed, evaluated here and nowhere else.
        bSlot = declare(".rb" + std::to_string(refTemps_), elemPtr, rpos);
        bName = ".rb" + std::to_string(refTemps_++);
        ExprPtr b(Var::local(bName, bSlot));
        b->setType(elemPtr);
        ExprPtr startAt(new Assign(std::move(b), decay(std::move(range))));
        startAt->setType(elemPtr);
        setup.push_back(StmtPtr(new ExprStmt(std::move(startAt))));
        if (kept) rangeBuilt_.push_back(std::make_pair(setup.size(), alive_.size()));

        // `T *__e = __b + N;`
        eSlot = declare(".re" + std::to_string(refTemps_), elemPtr, rpos);
        eName = ".re" + std::to_string(refTemps_++);
        ExprPtr from(Var::local(bName, bSlot));
        from->setType(elemPtr);
        ExprPtr count(new Num(rt->length()));
        count->setType(types_.get(target_.sizeType()));
        // **Through `arithmetic`, not a bare Binary.** `p + 1` on an `int *` advances four
        // bytes, and that scaling lives in the helper the ordinary expression path uses.
        // Built by hand it produced a loop that read the array one byte at a time.
        ExprPtr past = arithmetic(BinOp::Add, std::move(from), std::move(count),
                                  rpos);
        ExprPtr e(Var::local(eName, eSlot));
        e->setType(elemPtr);
        ExprPtr stopAt(new Assign(std::move(e), std::move(past)));
        stopAt->setType(elemPtr);
        setup.push_back(StmtPtr(new ExprStmt(std::move(stopAt))));
    }

    const std::vector<std::pair<std::size_t, std::size_t> > ownBuilt = rangeBuilt_;
    // A class iterator is walked by its own `!=`, `++` and `*`, [stmt.ranged]/1;
    // a pointer by the built-in ones.
    const bool classIter = elemPtr->isStructOrUnion();
    auto iterVar = [&](const std::string &n, int slot) {
        ExprPtr v(Var::local(n, slot));
        v->setType(elemPtr);
        return v;
    };
    ExprPtr deref;
    if (classIter) {
        ExprPtr it = iterVar(bName, bSlot);
        deref = overloadedUnary("*", it, rpos);
        if (deref == nullptr)
            src_.fail(rpos, "the iterator '" + elemPtr->describe() + "' has no "
                            "'operator*', which a range-based 'for' reads each element with");
    } else {
        deref.reset(new Unary('*', iterVar(bName, bSlot)));
        deref->setType(elemPtr->pointee());
    }
    const Type *elem = deref->type();
    if (mentionsDeduced(d.type))
        d.type = deduceAutoFrom(d.type, types_.referenceTo(elem), d.name, d.pos);   // `*__b` is an lvalue

    // `__b != __e`
    ExprPtr cond = comparison(BinOp::Ne, iterVar(bName, bSlot), iterVar(eName, eSlot), rpos);

    // `++__b`, or `__b = __b + 1` for a pointer
    ExprPtr step;
    if (classIter) {
        ExprPtr it = iterVar(bName, bSlot);
        step = overloadedUnary("++", it, rpos);
        if (step == nullptr)
            src_.fail(rpos, "the iterator '" + elemPtr->describe() + "' has no "
                            "prefix 'operator++', which a range-based 'for' steps with");
    } else {
        ExprPtr one(new Num(1LL));
        one->setType(types_.get(target_.sizeType()));
        ExprPtr next = arithmetic(BinOp::Add, iterVar(bName, bSlot), std::move(one), rpos);
        step.reset(new Assign(iterVar(bName, bSlot), std::move(next)));
        step->setType(elemPtr);
    }

    // The body, with the loop variable built from `*__b` in front of it.
    enterScope();
    const int inner = enterBlock();
    const int vSlot = declare(d.name, d.type, d.pos, quals.alignAs);
    ExprPtr take;
    if (d.type->isReference()) {
        // **`T &x : a` binds x to the element** - [stmt.ranged]/1 - so the
        // slot holds the element's address, read through as every reference is.
        const Type *slotType = types_.pointerTo(d.type->referent());
        ExprPtr var(Var::local(d.name, vSlot));
        var->setType(slotType);
        ExprPtr where = classIter
            ? bindReference(d.type, std::move(deref), d.pos, "'" + d.name + "'")
            : convert(iterVar(bName, bSlot), slotType);
        take.reset(new Assign(std::move(var), std::move(where)));
        take->setType(slotType);
    } else {
        ExprPtr at = std::move(deref);
        ExprPtr var(Var::local(d.name, vSlot));
        var->setType(d.type);
        take.reset(new Assign(std::move(var), convert(std::move(at), d.type)));
        take->setType(d.type);
    }

    std::vector<StmtPtr> body;
    body.push_back(StmtPtr(new ExprStmt(std::move(take))));
    loopDepth_++;
    loopMarks_.push_back(alive_.size());
    breakMarks_.push_back(alive_.size());
    body.push_back(statement());
    breakMarks_.pop_back();
    loopMarks_.pop_back();
    loopDepth_--;
    leaveBlock();
    leaveScope();
    Block *inside = new Block(std::move(body));
    inside->setScope(inner);

    For *f = new For(StmtPtr(), std::move(cond), std::move(step),
                     StmtPtr(inside));
    f->setScope(scope);
    setup.push_back(StmtPtr(f));
    // The range temporary and the iterators die after the loop, in the reverse order,
    // and on the way out of an exception too - a cleanup region from where each was built.
    if (alive_.size() != aliveMark) {
        std::vector<std::pair<std::size_t, std::size_t> > built;
        for (std::size_t i = 0; i < ownBuilt.size(); i++)
            if (ownBuilt[i].second > aliveMark &&
                (built.empty() || built.back().second != ownBuilt[i].second))
                built.push_back(ownBuilt[i]);
        emitDestructors(setup, aliveMark, rpos);
        setup = target_.microsoftNames()
                    ? wrapMsCleanups(std::move(setup), built, aliveMark, rpos)
                    : wrapCleanups(std::move(setup), built, aliveMark, rpos);
        alive_.resize(aliveMark);
    }

    leaveBlock();
    leaveScope();
    Block *whole = new Block(std::move(setup));
    whole->setScope(-1);
    return StmtPtr(whole);
}

namespace {
// The flag has to come back off however the declaration ends, because a
// diagnostic raised inside a Trial is a substitution failure and unwinds.
struct ConditionFlag {
    bool &f;
    explicit ConditionFlag(bool &b) : f(b) { f = true; }
    ~ConditionFlag() { f = false; }
};
}

// **[stmt.select]/2: an `if` may declare a name in its condition**, and that
// name is in scope in both arms.
ExprPtr Parser::ifConditionDeclaration(std::vector<StmtPtr> &setup) {
    std::string name;
    {
        ConditionFlag guard(conditionDecl_);
        conditionName_.clear();
        setup.push_back(declaration());
        name = conditionName_;
    }
    ExprPtr v = objectRef(name);
    if (v == nullptr)
        src_.fail(peek().pos, "'" + name + "' was declared in this condition "
                              "and cannot be read back");
    return v;
}

// **[stmt.iter]/2 creates and destroys the variable on every turn**, which is
// the whole difference from the `if` form: hoisting the initialiser out would
// evaluate it once and then loop for ever on the value it got.
ExprPtr Parser::whileConditionDeclaration() {
    StorageClass sc;
    Qualifiers quals;
    const Type *base = specifiers(&sc, &quals);
    if (sc != StorageNone)
        src_.fail(peek().pos, "a condition declares an ordinary object, so it "
                              "may not have a storage class");
    Declared d = declarator(base);
    if (d.name.empty())
        src_.fail(d.pos, "a condition declares a name and this declares none");
    if (!consume("="))
        src_.fail(peek().pos, "'" + d.name + "' is declared in a condition and "
                              "has no initialiser - there would be nothing "
                              "to test");
    ExprPtr init = decay(assign());
    if (mentionsDeduced(d.type))
        d.type = deduceAutoFrom(d.type, deductionArgType(*init), d.name, d.pos);
    // A class would have to be constructed and destroyed once per turn, and the construction is
    // written where the test is - so it is refused here and not in an `if`, where the object is
    // built once and the ordinary declaration path does all of it.
    if (d.type->isStructOrUnion() || d.type->isReference() || d.type->isArray())
        src_.fail(d.pos, "a '" + d.type->describe() + "' declared in the "
                         "condition of a loop is not supported yet - "
                         "[stmt.iter] builds it afresh on every turn, and only "
                         "a scalar can be written where the test is");
    const int slot = declare(d.name, d.type, d.pos, quals.alignAs);
    locals_.back().isConst = d.type->isConst();
    ExprPtr x(Var::local(d.name, slot));
    x->setType(d.type);
    ExprPtr set(new Assign(std::move(x), convert(std::move(init), d.type)));
    set->setType(d.type);
    return set;
}

// At a condition's declaration: whether it declares an object only a declaration can make - a class,
// a reference, an array, or one `auto` deduces - which the loops below build as a block each turn.
bool Parser::conditionDeclaresObject() {
    const std::size_t save = at_;
    StorageClass sc;
    Qualifiers quals;
    const Type *base = specifiers(&sc, &quals);
    const Declared d = declarator(base);
    at_ = save;
    return d.type->isStructOrUnion() || d.type->isReference() || d.type->isArray() || mentionsDeduced(d.type);
}

// [stmt.while]/2: `while (T t = e) s` makes and destroys t every turn, a `for`
// condition too with its step after ([stmt.for]/1); lowered as `for (;;)` over
// that block, `break` reaching the object, the step run once it is gone.
StmtPtr Parser::declaringLoop(std::size_t pos, bool forStep) {
    loopDepth_++;
    enterScope();
    const int inner = enterBlock();
    const std::size_t aliveMark = alive_.size();
    loopMarks_.push_back(aliveMark);
    breakMarks_.push_back(aliveMark);
    std::vector<StmtPtr> steps;
    ExprPtr cond = contextualScalar(ifConditionDeclaration(steps), peek().pos, "this condition");
    std::vector<std::pair<std::size_t, std::size_t> > built;
    if (alive_.size() != aliveMark) built.push_back(std::make_pair(steps.size(), alive_.size()));
    ExprPtr step;
    if (forStep) {
        expect(";");
        if (!peek().is(")")) step = endFullExpression(decay(expr()));
    }
    expect(")");
    ExprPtr stop(new Unary('!', std::move(cond)));
    stop->setType(types_.get(Kind::Bool));
    steps.push_back(StmtPtr(new If(std::move(stop),
                                   jumpLeaving(StmtPtr(new Break()), aliveMark, pos, handlersLeftByBreak()),
                                   StmtPtr())));
    steps.push_back(statement());
    breakMarks_.pop_back();
    loopMarks_.pop_back();
    loopDepth_--;
    if (alive_.size() != aliveMark) {
        emitDestructors(steps, aliveMark, pos);
        steps = target_.microsoftNames() ? wrapMsCleanups(std::move(steps), built, aliveMark, pos)
                                         : wrapCleanups(std::move(steps), built, aliveMark, pos);
        alive_.resize(aliveMark);
    }
    leaveBlock();
    leaveScope();
    Block *turn = new Block(std::move(steps));
    turn->setScope(inner);
    return StmtPtr(new For(StmtPtr(), ExprPtr(), std::move(step), StmtPtr(turn)));
}

StmtPtr Parser::forStatement() {
    const std::size_t pos = peek().pos;
    expect("for");
    expect("(");
    enterScope();
    int scope = enterBlock();
    // What the init-statement builds lives to the end of the for statement and no further.
    const std::size_t aliveAtEntry = alive_.size();

    if (atRangeFor()) return rangeForStatement(scope);

    StmtPtr init;
    if (!consume(";")) {
        if (atDeclarationStart()) init = declaration();
        else { ExprPtr e = endFullExpression(expr()); expect(";"); init = StmtPtr(new ExprStmt(std::move(e))); }
    }

    StmtPtr loop;
    ExprPtr cond, step;
    StmtPtr body;
    if (!peek().is(";") && atConditionDeclaration() && conditionDeclaresObject()) {
        loop = declaringLoop(pos, true);
    } else {
        if (!peek().is(";")) cond = endFullExpression(decay(expr()));
        expect(";");

        if (!peek().is(")")) step = endFullExpression(decay(expr()));
        expect(")");

        // The mark is taken after the init-statement: `for (S s; ...)` builds s
        // once for the whole loop, and a break must not destroy it.
        loopDepth_++;
        loopMarks_.push_back(alive_.size());
        breakMarks_.push_back(alive_.size());
        body = statement();
        breakMarks_.pop_back();
        loopMarks_.pop_back();
        loopDepth_--;
    }

    leaveBlock();
    leaveScope();
    if (alive_.size() == aliveAtEntry) {
        if (loop == nullptr) {
            For *f = new For(std::move(init), std::move(cond), std::move(step), std::move(body));
            f->setScope(scope);
            return StmtPtr(f);
        }
        std::vector<StmtPtr> both;
        if (init != nullptr) both.push_back(std::move(init));
        both.push_back(std::move(loop));
        Block *b = new Block(std::move(both));
        b->setScope(scope);
        return StmtPtr(b);
    }
    if (loop == nullptr) loop.reset(new For(StmtPtr(), std::move(cond), std::move(step), std::move(body)));

    // **`for (S s; ...)` destroys s when the loop is done**, the way a block destroys what it built
    // at its '}' - and on the way out of an exception, so the init stands in front of the loop, in
    // the statement's scope, and the loop is a cleanup region from there, as an `if`'s condition is.
    std::vector<StmtPtr> steps;
    steps.push_back(std::move(init));
    steps.push_back(std::move(loop));
    const std::vector<std::pair<std::size_t, std::size_t> > built(1, std::make_pair(std::size_t(1), alive_.size()));
    emitDestructors(steps, aliveAtEntry, pos);
    steps = target_.microsoftNames() ? wrapMsCleanups(std::move(steps), built, aliveAtEntry, pos)
                                     : wrapCleanups(std::move(steps), built, aliveAtEntry, pos);
    alive_.resize(aliveAtEntry);
    Block *b = new Block(std::move(steps));
    b->setScope(scope);
    return StmtPtr(b);
}

// **`static_assert(cond, "message");` - a declaration that declares nothing and emits
// nothing.** The message is required in C++11, so the one-argument form is refused by
// name; the condition must be an integral constant expression, narrower than the rule.

StmtPtr Parser::switchStatement() {
    std::size_t pos = peek().pos;
    expect("switch");
    expect("(");
    ExprPtr cond = contextualScalar(endFullExpression(decay(expr())), pos,
                                   "this condition");
    if (!cond->type()->isInteger())
        src_.fail(pos, "a switch needs an integer, not '" +
                       cond->type()->describe() + "'");
    const Type *governing = promote(cond->type());
    cond = convert(std::move(cond), governing);
    expect(")");

    switches_.push_back(SwitchCtx{ {}, nullptr, governing, jumpGuards() });
    switchDepth_++;
    breakMarks_.push_back(alive_.size());
    StmtPtr body = statement();
    breakMarks_.pop_back();
    switchDepth_--;

    SwitchCtx ctx = std::move(switches_.back());
    switches_.pop_back();
    return StmtPtr(new Switch(std::move(cond), std::move(body),
                              std::move(ctx.cases), ctx.deflt));
}

StmtPtr Parser::caseLabel() {
    std::size_t pos = peek().pos;
    bool isDefault = consume("default");
    if (!isDefault) expect("case");

    if (switches_.empty())
        src_.fail(pos, isDefault ? "'default' is not inside a switch"
                                 : "'case' is not inside a switch");

    long long value = 0;
    if (isDefault) {
        if (switches_.back().deflt)
            src_.fail(pos, "a switch has only one 'default'");
    } else {
        // [stmt.switch]/2: a converted constant expression, and a scoped enumeration converts to and from nothing.
        const Type *governing = switches_.back().governing;
        const std::size_t at = peek().pos;
        ExprPtr e = decay(conditional());
        const Type *et = e->type()->unqualified();
        if ((governing->isScopedEnumeration() || et->isScopedEnumeration()) &&
            et != governing->unqualified())
            src_.fail(at, "value of type '" + et->describe() + "' is not implicitly "
                          "convertible to '" + governing->describe() + "'");
        long long folded;
        if (!fold(*e, &folded, at))
            src_.fail(at, "expected a case value, and this is not an integer constant expression");
        value = narrowTo(folded, governing);
        for (const Case *c : switches_.back().cases)
            if (c->value() == value)
                src_.fail(pos, "duplicate case value " + std::to_string(value));
    }
    expect(":");

    // **Every case label is a jump from the `switch`**, so what is in scope
    // here and was not there has been jumped past. [stmt.dcl]/3 names the
    // switch alongside goto, and clang refuses it the same way.
    checkJump(switches_.back().guards, jumpGuards(), pos,
              isDefault ? std::string("'default:'")
                        : "'case " + std::to_string(value) + ":'",
              "the 'switch'");

    // A declaration is a statement in C++, so a label may stand before one;
    // the rule that it may not was C's (the cl review's A10).
    if (peek().is("}"))
        src_.fail(peek().pos, "a label must be followed by a statement");

    StmtPtr body = atDeclarationStart() ? declaration() : statement();

    Case *node = new Case(value, isDefault, caseIds_++, std::move(body));
    StmtPtr owned(node);
    SwitchCtx &sw = switches_.back();
    if (isDefault) sw.deflt = node;
    else sw.cases.push_back(node);
    return owned;
}

StmtPtr Parser::gotoLabel() {
    std::size_t pos = peek().pos;
    std::string name = expectIdent("a label");
    expect(":");

    for (const LabelDef &l : labels_)
        if (l.name == name)
            src_.fail(pos, "label '" + name + "' is defined twice in this function");
    labels_.push_back(LabelDef{ name, pos, jumpGuards(), alive_, nullptr, true, std::string() });

    // A declaration is a statement in C++, so a label may stand before one;
    // the rule that it may not was C's (the cl review's A10).
    if (peek().is("}"))
        src_.fail(peek().pos, "a label must be followed by a statement");

    return StmtPtr(new Label(std::move(name), atDeclarationStart() ? declaration() : statement()));
}

// **[stmt.dcl]/3: a jump may not enter the scope of an initialised object.** All three
// jumps this compiler has fall under it, and clang refuses each. The test is set
// membership: refused when the label's live list holds one the origin's does not.
std::vector<Parser::JumpGuard> Parser::jumpGuards() const {
    std::vector<JumpGuard> out;
    for (const Local &l : locals_)
        if (l.guardsJump) out.push_back(JumpGuard{ l.name, l.serial, 0 });
    // An open `try` block or handler, keyed by where it begins: no local's serial is negative.
    for (std::size_t p : tryFrom_) out.push_back(JumpGuard{ "", -2 * static_cast<int>(p) - 1, 1 });
    for (std::size_t p : handlerFrom_) out.push_back(JumpGuard{ "", -2 * static_cast<int>(p) - 2, 2 });
    return out;
}

// A jump that leaves a scope destroys what the scope built, innermost first, before it goes.
void Parser::endCatches(std::vector<StmtPtr> &into, int count) {
    // **Nothing to call on the Microsoft ABI**: a handler there is a funclet
    // and the runtime ends the catch when it returns, so the counts this asks
    // for are used only to refuse the jumps that would leave one early.
    if (target_.microsoftNames()) return;
    for (int i = 0; i < count; i++)
        into.push_back(StmtPtr(new ExprStmt(
            runtimeCall("__cxa_end_catch", types_.get(Kind::Void),
                        std::vector<ExprPtr>()))));
}

// **Where an exception leaving this point goes, in this frame.**
std::string Parser::unwindTarget(int *ptrSlot, int *selSlot) const {
    if (!handlerResumeLabel_.empty()) {
        *ptrSlot = handlerResumePtr_;
        *selSlot = handlerResumeSel_;
        unwindTargetsAsked_++;
        return handlerResumeLabel_;
    }
    if (!tryChainLabel_.empty()) {
        *ptrSlot = tryChainPointerSlot_;
        *selSlot = tryChainSelectorSlot_;
        return tryChainLabel_;
    }
    return std::string();
}

// **A `break` leaves a handler exactly when the loop or switch it breaks out
// of was entered before that handler was.**
int Parser::handlersLeftByBreak() const {
    int left = 0;
    for (std::size_t i = handlerLoopDepth_.size(); i-- > 0; ) {
        if (loopDepth_ != handlerLoopDepth_[i] ||
            switchDepth_ != handlerSwitchDepth_[i]) break;
        left++;
    }
    return left;
}

int Parser::handlersLeftByContinue() const {
    int left = 0;
    for (std::size_t i = handlerLoopDepth_.size(); i-- > 0; ) {
        if (loopDepth_ != handlerLoopDepth_[i]) break;
        left++;
    }
    return left;
}

// **A fresh slot per region, never one cached here**: an implicit member is synthesized
// mid-function with frameSize_ zeroed and put back, so a cached slot lands in the wrong frame.
int Parser::msUnwindHelp() {
    return allocateFrameSlot(types_.pointerTo(types_.get(Kind::Void)));
}

std::string Parser::msExitLabel(std::string &label, const char *kind) {
    if (label.empty()) label = std::string("$ms") + kind + std::to_string(refTemps_++);
    return label;
}

StmtPtr Parser::jumpLeaving(StmtPtr jump, std::size_t mark, std::size_t pos,
                            int endsCatches) {
    std::vector<StmtPtr> steps;
    emitDestructors(steps, mark, pos);
    endCatches(steps, endsCatches);
    if (steps.empty()) return jump;
    steps.push_back(std::move(jump));
    Block *b = new Block(std::move(steps));
    b->setScope(-1);
    return StmtPtr(b);
}

// [class.dtor]/8: a `return` in a destructor goes on to the members and the bases.
StmtPtr Parser::leaveBody() {
    if (dtorEndLabel_.empty()) return StmtPtr(new Return(nullptr));
    dtorReturned_ = true;
    return StmtPtr(new Goto(dtorEndLabel_));
}

// **A class whose copy is a call is returned where the caller asked for it** (1.81). Until 1.80 the returned
// object was built in a slot of this frame and its bytes moved to the caller's, its destructor skipped - an
// elision that kept no constructor's promise: an object that holds its own address (the end node of
// std::map, std::set and std::list, a `self` pointer) pointed back into the dead frame, and every map returned
// by value walked on past its end. A temporary of the class is now built at the caller's address
// ([class.copy]/31); anything else - an automatic object, a parameter, a call's result, a member of a
// temporary - is moved there, or copied, by its constructor ([class.copy]/32), and destroyed where it is.
// Whether `name`, declared here with `type`, is the function's named return value: an object of the class the
// function returns, whose copy is a call, and every `return` between here and the end of its block returns it
// by that name - with nothing there declaring the name again, which would make a `return` mean another object.
// The scan is of tokens and keeps to the safe side: what it cannot tell, it takes for a declaration.
bool Parser::namedReturnCandidate(const std::string &name, const Type *type) const {
    if (returnType_ == nullptr || returnType_->isReference() || type->isReference() || type->isArray() ||
        type->isVolatile())
        return false;
    const Type *t = type->unqualified();
    if (!t->isStructOrUnion() || t != returnType_->unqualified() || !t->nonTrivialCopy()) return false;
    if (moveConstructorOf(t) == nullptr && copyConstructorOf(t) == nullptr) return false;
    bool returned = false;
    int depth = 0;
    for (std::size_t i = at_; i + 2 < tokens_.size() && tokens_[i].kind != TokenKind::End; i++) {
        const Token &k = tokens_[i];
        if (k.is("{")) { depth++; continue; }
        if (k.is("}")) { if (depth-- == 0) break; continue; }
        if (k.kind == TokenKind::Keyword && k.is("return")) {
            if (tokens_[i + 1].kind != TokenKind::Ident || tokens_[i + 1].text != name || !tokens_[i + 2].is(";"))
                return false;
            returned = true;
            continue;
        }
        if (k.kind != TokenKind::Ident || k.text != name || i == 0) continue;
        const Token &before = tokens_[i - 1], &after = tokens_[i + 1];
        const bool typeBefore = before.kind == TokenKind::Ident || before.is(">") || before.is("*") ||
                                before.is("&") || before.is("&&") ||
                                (before.kind == TokenKind::Keyword && !before.is("return") && !before.is("throw") &&
                                 !before.is("case") && !before.is("goto"));
        const bool declAfter = after.is(";") || after.is("(") || after.is("{") || after.is("=") || after.is(",") ||
                               after.is("[") || after.is(")");
        if (typeBefore && declAfter) return false;
    }
    return returned;
}

bool Parser::namedReturnInScope() const {
    if (nrvoSlot_ == 0) return false;
    const Local *l = findLocal(nrvoName_);
    // The same name at the same offset may be another object, once the first's block has ended and given its
    // slot back.
    return l != nullptr && l->offset == nrvoSlot_ && l->type->isReference() && returnType_ != nullptr &&
           l->type->referent()->unqualified() == returnType_->unqualified();
}

StmtPtr Parser::returnInPlace(ExprPtr returned, std::size_t pos, bool leavesFunclet) {
    const Type *cls = returnType_->unqualified();
    // **The named return value is already the caller's object**: nothing is built, and everything else alive
    // is destroyed. The move or copy constructor an elision leaves out is still the one [class.copy]/32 chooses.
    if (namedReturnInScope()) {
        const Unary *u = dynamic_cast<const Unary *>(returned.get());
        const Var *v = u != nullptr && u->op() == '*' ? dynamic_cast<const Var *>(&u->operand()) : nullptr;
        if (v == nullptr || !v->isLocal() || v->offset() != nrvoSlot_)
            src_.fail(pos, "this 'return' is in the scope of '" + nrvoName_ + "', which the function returns by "
                           "name, and returns something else - not supported yet; return '" + nrvoName_ +
                           "', or give the other value a block of its own");
        const Signature *mc = moveConstructorOf(cls);
        const Signature *sel = mc != nullptr && !mc->deleted ? mc : copyConstructorOf(cls);
        if (mc != nullptr && mc->deleted)
            src_.fail(pos, "'" + returnType_->describe() + "' has a deleted move constructor, and returning '" +
                           nrvoName_ + "' would choose it - [class.copy]/32");
        if (sel != nullptr && (sel->deleted || sel->isExplicit))
            src_.fail(pos, "'" + returnType_->describe() + "' cannot be returned by value - its " +
                           (sel == mc ? "move" : "copy") + " constructor is " + (sel->deleted ? "deleted" : "explicit"));
        std::vector<StmtPtr> out;
        out.push_back(StmtPtr(new ExprStmt(endFullExpression(std::move(returned)))));
        expect(";");
        ExprPtr result(new Unary('*', [&]() { ExprPtr p(Var::local(".sret", sretSlot_));
                                              p->setType(types_.pointerTo(cls)); return p; }()));
        result->setType(returnType_);
        if (!alive_.empty() || handlerDepth_ > 0) {
            for (std::size_t i = alive_.size(); i > 0; i--) {
                const Alive &a = alive_[i - 1];
                if (a.byAddress && a.offset == nrvoSlot_) continue;
                destroyObject(out, a, pos);
            }
            endCatches(out, handlerDepth_);
            if (leavesFunclet) {
                if (msExit_->retSlot == 0) {
                    msExit_->retSlot = allocateFrameSlot(returnSlotType());
                    msExit_->retTemp = ".ret" + std::to_string(refTemps_++);
                }
                ExprPtr keep(Var::local(msExit_->retTemp, msExit_->retSlot));
                keep->setType(returnSlotType());
                ExprPtr save(new Assign(std::move(keep), std::move(result)));
                save->setType(returnSlotType());
                out.push_back(StmtPtr(new ExprStmt(std::move(save))));
                out.push_back(StmtPtr(new FuncletLeave(msExitLabel(msExit_->ret, "ret"))));
                return StmtPtr(new Block(std::move(out)));
            }
        }
        out.push_back(StmtPtr(new Return(std::move(result))));
        return StmtPtr(new Block(std::move(out)));
    }
    auto caller = [&]() {
        ExprPtr p(Var::local(".sret", sretSlot_));
        p->setType(types_.pointerTo(cls));
        return p;
    };
    checkAssignable(*returned, returnType_, pos, "this function's return type");
    // [class.copy]/31: the constructor an elided copy would have called is checked all the same.
    if (returned->type() != nullptr && returned->type()->unqualified() == cls) {
        const Signature *mc = moveConstructorOf(cls);
        const Signature *sel = mc != nullptr ? mc : copyConstructorOf(cls);
        if (sel != nullptr && sel->isExplicit)
            src_.fail(pos, "'" + returnType_->describe() + "' has an 'explicit' " + (sel == mc ? "move" : "copy") +
                           " constructor, so it cannot be returned by value - 'return' copy-initialises the "
                           "caller's object, and that may not pick an explicit constructor even where the copy "
                           "is elided");
    }
    std::vector<StmtPtr> out;
    ExprPtr at = caller();
    if (constructInPlace(*returned, cls, at)) {
        out.push_back(StmtPtr(new ExprStmt(endFullExpression(std::move(returned)))));
    } else {
        // [class.copy]/32: an automatic object of the returned class, or a parameter, is moved first.
        bool automatic = false;
        if (returned->type() != nullptr && returned->type()->unqualified() == cls) {
            const Expr *named = returned.get();
            bool viaDeref = false;
            if (const Unary *u = dynamic_cast<const Unary *>(named))
                if (u->op() == '*') { named = &u->operand(); viaDeref = true; }
            if (const Var *v = dynamic_cast<const Var *>(named))
                if (v->isLocal())
                    if (const Local *l = findLocal(v->name()))
                        automatic = viaDeref ? l->isParameter && l->byValueByAddress : !l->type->isReference();
        }
        if (automatic && moveConstructorOf(cls) != nullptr) returned->setXvalue();
        Init clause;
        clause.value = std::move(returned);
        clause.pos = pos;
        ExprPtr made = constructListedElement(cls, clause, std::move(at), pos);
        out.push_back(StmtPtr(new ExprStmt(endFullExpression(std::move(made)))));
    }
    expect(";");
    auto result = [&]() {
        ExprPtr r(new Unary('*', caller()));
        r->setType(returnType_);
        return r;
    };
    if (!alive_.empty() || handlerDepth_ > 0) {
        emitDestructors(out, 0, pos);
        endCatches(out, handlerDepth_);
        // A handler's funclet hands the value to its parent, which returns it: the parent copies the
        // saved bytes back to the caller's object, where they already are.
        if (leavesFunclet) {
            if (msExit_->retSlot == 0) {
                msExit_->retSlot = allocateFrameSlot(returnSlotType());
                msExit_->retTemp = ".ret" + std::to_string(refTemps_++);
            }
            ExprPtr keep(Var::local(msExit_->retTemp, msExit_->retSlot));
            keep->setType(returnSlotType());
            ExprPtr save(new Assign(std::move(keep), result()));
            save->setType(returnSlotType());
            out.push_back(StmtPtr(new ExprStmt(std::move(save))));
            out.push_back(StmtPtr(new FuncletLeave(msExitLabel(msExit_->ret, "ret"))));
            return StmtPtr(new Block(std::move(out)));
        }
    }
    out.push_back(StmtPtr(new Return(result())));
    return StmtPtr(new Block(std::move(out)));
}

void Parser::checkJump(const std::vector<JumpGuard> &from,
                       const std::vector<JumpGuard> &to, std::size_t pos,
                       const std::string &jump, const std::string &origin) const {
    for (const JumpGuard &g : to) {
        bool inScopeAtOrigin = false;
        for (const JumpGuard &f : from)
            if (f.serial == g.serial) { inScopeAtOrigin = true; break; }
        if (inScopeAtOrigin) continue;
        if (g.region != 0)
            src_.fail(pos, jump + " enters a " + (g.region == 1 ? "'try' block" : "'catch' handler") +
                           " from outside it - [except.handle]/2 lets a jump leave one and never "
                           "enter it; put the label outside it");
        src_.fail(pos, jump + " jumps past the initialisation of '" + g.name +
                       "', which is in scope at the label and not at " +
                       origin + " - a jump may not enter the scope of a "
                       "variable that has an initialiser, a constructor or a "
                       "destructor, because the object would be used and "
                       "destroyed without ever having been built. Declare '" +
                       g.name + "' before " + origin + ", or put it in a "
                       "block that ends before the label");
    }
}

// **The jump destroys what it leaves, and cannot yet know what that is**: a forward label has not
// been read, so the goto is an empty block resolveGotos() fills, destructors and jump alike.
StmtPtr Parser::gotoStatement(const std::string &name, std::size_t pos, bool destroys) {
    Block *cleanups = new Block({});
    cleanups->setScope(-1);
    LabelDef g{ name, pos, jumpGuards(), alive_, cleanups, destroys, std::string() };
    // Inside a Microsoft handler the jump may leave the funclet, by an exit its `try` places after it.
    if (inMsHandler_ && msExit_ != nullptr) {
        std::string exit;
        g.msExit = msExitLabel(exit, "goto");
        msExit_->gotos.push_back(std::make_pair(g.msExit, name));
    }
    gotos_.push_back(g);
    return StmtPtr(cleanups);
}

void Parser::resolveGotos() {
    for (const LabelDef &g : gotos_) {
        const LabelDef *target = nullptr;
        for (const LabelDef &l : labels_)
            if (l.name == g.name) { target = &l; break; }
        if (target == nullptr)
            src_.fail(g.pos, "no label '" + g.name + "' in this function");
        checkJump(g.guards, target->guards, g.pos, "'goto " + g.name + "'",
                  "the goto");

        // **A goto out of a scope destroys what it leaves**, innermost first, in the
        // block left in front of the Goto for them: alive at the goto and not at the
        // label is left behind, alive at both is stayed inside. Never twice.
        for (std::size_t i = g.destroys ? g.alive.size() : 0; i > 0; i--) {
            const Alive &a = g.alive[i - 1];
            bool atLabel = false;
            for (const Alive &b : target->alive)
                if (b.offset == a.offset) { atLabel = true; break; }
            if (atLabel) continue;
            std::vector<StmtPtr> call;
            destroyObject(call, a, g.pos);
            for (std::size_t k = 0; k < call.size(); k++)
                g.cleanups->append(std::move(call[k]));
        }
        // **And it ends the handling of each handler it leaves**, [except.handle]/16: a call each on
        // Itanium; a Microsoft funclet is left for the exit its parent jumps on from.
        int leaves = 0;
        for (const JumpGuard &f : g.guards) {
            bool kept = f.region != 2;
            for (const JumpGuard &t : target->guards) kept = kept || t.serial == f.serial;
            leaves += kept ? 0 : 1;
        }
        if (leaves > 0 && !g.msExit.empty()) {
            g.cleanups->append(StmtPtr(new FuncletLeave(g.msExit)));
            continue;
        }
        std::vector<StmtPtr> ends;
        endCatches(ends, leaves);
        for (std::size_t k = 0; k < ends.size(); k++) g.cleanups->append(std::move(ends[k]));
        g.cleanups->append(StmtPtr(new Goto(g.name)));
    }
    labels_.clear();
    gotos_.clear();
}

StmtPtr Parser::block(bool functionTry) {
    std::size_t pos = peek().pos;
    const Nesting deep(this, pos, "block");
    if (!functionTry) expect("{");
    enterScope();
    // A `using namespace` written inside this block reaches the '}' and no
    // further - [namespace.udir]/2 - so the list is cut back to what it held
    // on the way in.
    const std::size_t usingAtEntry = usingNamespaces_.size();
    const std::size_t undoAtEntry = blockUndo_.size();
    const std::size_t aliveAtEntry = alive_.size();
    const std::size_t typesAtEntry = localTypeUndo_.size();
    const std::size_t outerTypeStart = localTypeBlockStart_;
    localTypeBlockStart_ = typesAtEntry;
    bool isBody = atFunctionBody_;
    atFunctionBody_ = false;
    const std::size_t outerBody = bodyScope_;
    if (isBody) bodyScope_ = scopeStarts_.size();
    // The regions of a function body reach back to its by-value parameters
    // on the target whose callee destroys them; every other block's start at
    // what was alive when it opened.
    const std::size_t regionFrom = isBody && bodyCleanupFrom_ < aliveAtEntry
                                 ? bodyCleanupFrom_ : aliveAtEntry;
    int scope = isBody ? 0 : enterBlock();
    // **Where each object became alive**, as a statement index and how many were alive
    // after it. A cleanup region runs from one of these to the next and destroys
    // exactly what was built by then, so an exception cannot destroy what is not.
    std::vector<std::pair<std::size_t, std::size_t> > built;
    std::vector<std::size_t> tryAt;
    std::vector<StmtPtr> body;
    // **What this block's statements made and destroyed within themselves.**
    std::vector<Temporary> temps;
    std::vector<Temporary> outer;
    outer.swap(statementTemps_);
    while (functionTry ? body.empty() : !peek().is("}")) {
        if (peek().kind == TokenKind::End)
            src_.fail(peek().pos, "unclosed '{'");
        const std::size_t aliveBefore = alive_.size();
        body.push_back(atDeclarationStart() ? declaration() : statement());
        // **A `try` is not covered by a cleanup region, it answers for
        // itself.**
        if (dynamic_cast<const Try *>(body.back().get()) != nullptr)
            tryAt.push_back(body.size() - 1);
        for (std::size_t k = 0; k < statementTemps_.size(); k++)
            temps.push_back(statementTemps_[k]);
        statementTemps_.clear();
        if (alive_.size() > aliveBefore) {
            guardThrowingDestructors(body, aliveAtEntry, aliveBefore);
            built.push_back(std::make_pair(body.size(), alive_.size()));
        }
    }
    statementTemps_.swap(outer);
    // A region has to cover the statements that made them, and the first may
    // be before anything was alive - so the regions start at the top of the
    // block rather than at the first construction.
    if ((!temps.empty() || regionFrom != aliveAtEntry) &&
        (built.empty() || built[0].first != 0))
        built.insert(built.begin(), std::make_pair(std::size_t(0), aliveAtEntry));

    // Everything this block constructed is destroyed here, last first.
    emitDestructors(body, aliveAtEntry, peek().pos);

    if (!built.empty()) {
        // **A `try` among these statements is split around, not refused.** It
        // is a row of its own and its pad destroys what it found alive.
        body = target_.microsoftNames()
                   ? wrapMsCleanups(std::move(body), built, regionFrom, pos,
                                    temps)
                   : wrapCleanups(std::move(body), built, regionFrom, pos,
                                  temps, tryAt);
    }
    alive_.resize(aliveAtEntry);
    usingNamespaces_.resize(usingAtEntry);
    while (blockUndo_.size() > undoAtEntry) {
        const BlockUndo &u = blockUndo_.back();
        if (u.was.empty()) u.table->erase(u.key);
        else (*u.table)[u.key] = u.was;
        blockUndo_.pop_back();
    }
    while (localTypeUndo_.size() > typesAtEntry) {
        if (localTypeUndo_.back().second == nullptr) localTypes_.erase(localTypeUndo_.back().first);
        else localTypes_[localTypeUndo_.back().first] = localTypeUndo_.back().second;
        localTypeUndo_.pop_back();
    }
    localTypeBlockStart_ = outerTypeStart;

    if (!functionTry) expect("}");
    if (!isBody) leaveBlock();
    leaveScope();
    bodyScope_ = outerBody;
    Block *b = new Block(std::move(body));
    b->setScope(scope);

    b->setPos(pos);
    return StmtPtr(b);
}

StmtPtr Parser::statement() {
    std::size_t pos = peek().pos;
    const ReadingAt reading(this, &stmtAt_);
    // An `else if` chain is one statement under another, each arm a level.
    const Nesting deep(this, pos, "statement");
    StmtPtr s = statementBody();
    if (s) s->setPos(pos);
    forgetUnsetArguments();
    return s;
}

// **`if (a = b)`**: well-formed, and rarely what was meant; a second pair of parentheses says that it was.
void Parser::warnAssignmentAsCondition() {
    int depth = 0;
    for (std::size_t k = 0;; k++) {
        const Token &t = peekAt(k);
        if (t.kind == TokenKind::End) return;
        if (t.is("(") || t.is("[") || t.is("{")) depth++;
        else if (t.is(")") || t.is("]") || t.is("}")) { if (depth-- == 0) return; }
        else if (depth == 0 && t.is("=")) {
            src_.warn(t.pos, "an assignment as a condition: '==' may have been meant; parentheses around it say it was not");
            return;
        }
    }
}

// **A local read before anything is written to it**, by the order of the text: the first mention decides. `x =` and
// `&x` and `>> x` write it, or may; `int &r = x` is such a mention too. A call's argument is left to the call, which
// knows whether a reference binds it. Whatever a branch or a loop does to that order, a wrong answer can only
// be a warning, and the worst of them is one on a loop that reads what its last turn wrote.
void Parser::noteUnsetLocal(const Local &l, std::size_t pos) {
    if (unevaluated_ > 0 || at_ == 0) return;
    const Token &before = tokens_[at_ - 1], &after = peekAt(1);
    const bool bound = at_ >= 3 && before.is("=") && (tokens_[at_ - 3].is("&") || tokens_[at_ - 3].is("&&"));
    if (after.is("=") || before.is("&") || before.is(">>") || bound) { l.unset = false; return; }
    const bool held = at_ >= 2 && before.is("(") && (tokens_[at_ - 2].is("if") || tokens_[at_ - 2].is("while") || tokens_[at_ - 2].is("switch"));
    if (!held && (before.is("(") || before.is(",")) && (after.is(")") || after.is(","))) {
        unsetArguments_[l.name] = pos;
        return;
    }
    l.unset = false;
    src_.warn(pos, "'" + l.name + "' is read here and nothing has been assigned to it");
}

// **A local bound to a non-const reference parameter is written by the call**, or may be, as `&x` may: it is an
// assignment from here on, so `if (special(x, result)) return result;` reads what the call left. A const reference
// is left to the end of the statement as before - a read the call may make, and nothing this can follow.
void Parser::noteUnsetArguments(const std::vector<ExprPtr> &args, const std::vector<const Type *> &params) {
    if (unsetArguments_.empty()) return;
    for (std::size_t i = 0; i < args.size(); i++) {
        const Type *param = i < params.size() ? params[i] : nullptr;
        const bool byReference = param != nullptr && param->isReference();
        if (byReference && param->referent()->isConst()) continue;
        const Expr *e = args[i].get();
        for (;;) {
            if (const Cast *c = dynamic_cast<const Cast *>(e)) { e = &c->value(); continue; }
            if (const Unary *u = dynamic_cast<const Unary *>(e)) {
                if (byReference && u->op() == '&') { e = &u->operand(); continue; }
            }
            break;
        }
        const Var *v = dynamic_cast<const Var *>(e);
        if (v == nullptr || !v->isLocal()) continue;
        const std::map<std::string, std::size_t>::iterator at = unsetArguments_.find(v->name());
        const Local *l = at == unsetArguments_.end() ? nullptr : findLocal(v->name());
        if (l == nullptr || !l->unset) continue;
        l->unset = false;
        if (!byReference) src_.warn(at->second, "'" + l->name + "' is read here and nothing has been assigned to it");
        unsetArguments_.erase(at);
    }
}

// What no call read by value was bound to a reference, or went where this cannot follow it.
void Parser::forgetUnsetArguments() {
    for (const std::pair<const std::string, std::size_t> &a : unsetArguments_)
        if (const Local *l = findLocal(a.first)) l->unset = false;
    unsetArguments_.clear();
}

// A listed type is caught as `catch (T &)` would catch it, [except.spec]/8, and named by no slot.
std::vector<MsHandler> Parser::msSpecHandlers(const std::vector<const Type *> &allowed, StmtPtr last,
                                              std::size_t pos) {
    std::vector<MsHandler> handlers;
    for (const Type *t : allowed) {
        MicrosoftThrow names;
        std::string why;
        if (!microsoftThrowNames(t, t->size(target_), &names, &why))
            src_.fail(pos, "a 'throw(...)' list cannot name '" + t->describe() + "': " + why);
        finishMicrosoftThrow(names, false);
        MsHandler h;
        h.pos = pos;
        h.descriptor = names.descriptor;
        h.objectSize = t->size(target_);
        h.byReference = true;
        h.constPointer = names.isConst;
        mayThrow_++;
        std::vector<StmtPtr> rethrow;
        rethrow.push_back(StmtPtr(new ExprStmt(rethrowCall())));
        h.body = StmtPtr(new Block(std::move(rethrow)));
        handlers.push_back(std::move(h));
    }
    MsHandler all;
    all.pos = pos;
    std::vector<StmtPtr> body;
    body.push_back(std::move(last));
    all.body = StmtPtr(new Block(std::move(body)));
    handlers.push_back(std::move(all));
    return handlers;
}

// **`try` is a block, a landing pad, and no new statement machinery.**
StmtPtr Parser::tryStatement(std::size_t pos, const std::function<StmtPtr()> &bodyFn, bool rethrows,
                             const std::string &spec, const std::vector<const Type *> *allowed) {
    // What is alive when the `try` is reached.
    const std::size_t aliveOutside = alive_.size();
    // **The two ABIs disagree about who picks the handler**, so this reads one grammar
    // and builds two shapes: Itanium's if/else chain on a selector, and Microsoft's
    // handlers kept whole for the runtime to call.
    const bool microsoft = target_.microsoftNames();
    functionHasTry_ = true;

    const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
    // **A nested `try` shares the slots of the one it sits in.**
    const bool nested = !microsoft && !tryChainLabel_.empty();
    const int pointerSlot = nested ? tryChainPointerSlot_
                                   : allocateFrameSlot(voidPtr);
    const int selectorSlot = nested ? tryChainSelectorSlot_
                                    : allocateFrameSlot(types_.intType());
    functionHasPads_ = true;

    // **The chain the body's cleanup rows hand over to.**
    const std::string chainLabel = "$chain" + std::to_string(refTemps_++);
    // **The chain of the `try` this one sits inside**, captured before this
    // statement overwrites it. A nested `try` that matches nothing must reach
    // the enclosing handlers rather than leave the frame.
    const std::string enclosingChain = tryChainLabel_;
    const std::string wasChain = tryChainLabel_;
    const int wasPtr = tryChainPointerSlot_, wasSel = tryChainSelectorSlot_;
    const std::size_t wasAliveFrom = tryChainAliveFrom_;
    std::vector<Try *> wasSegments;
    wasSegments.swap(tryBodySegments_);
    // **Inside this body the innermost answer is this `try`'s own chain**, not
    // the pad of a handler this `try` happens to sit in - that one is reached
    // from the end of this chain, a step further out.
    const std::string wasHandlerLabel = handlerResumeLabel_;
    const int wasHandlerPtr = handlerResumePtr_, wasHandlerSel = handlerResumeSel_;
    if (!microsoft) {
        tryChainLabel_ = chainLabel;
        tryChainPointerSlot_ = pointerSlot;
        tryChainSelectorSlot_ = selectorSlot;
        tryChainAliveFrom_ = aliveOutside;
        handlerResumeLabel_.clear();
    }

    const bool wasInTry = inTryBody_;
    const int wasTrySerial = tryBodySerial_;
    inTryBody_ = true;
    tryBodySerial_ = localSerial_;
    if (!bodyFn && !peek().is("{"))
        src_.fail(peek().pos, "'try' takes a block");
    tryFrom_.push_back(peek().pos);
    StmtPtr body = bodyFn ? bodyFn() : block();
    tryFrom_.pop_back();
    inTryBody_ = wasInTry;
    tryBodySerial_ = wasTrySerial;
    handlerResumeLabel_ = wasHandlerLabel;
    handlerResumePtr_ = wasHandlerPtr;
    handlerResumeSel_ = wasHandlerSel;

    // Off before the handlers: a handler's block is not inside this row.
    std::vector<Try *> segments;
    segments.swap(tryBodySegments_);
    tryBodySegments_.swap(wasSegments);
    tryChainLabel_ = wasChain;
    tryChainPointerSlot_ = wasPtr;
    tryChainSelectorSlot_ = wasSel;
    tryChainAliveFrom_ = wasAliveFrom;

    if (spec.empty() && !peek().is("catch"))
        src_.fail(peek().pos, "a 'try' needs at least one 'catch'");

    // Read the handlers innermost-last, so the chain can be built from the
    // bottom: what nothing matches is _Unwind_Resume, and each handler wraps
    // what came before it as its else.
    struct Handler {
        std::string type;         // the _ZTI symbol, empty for catch (...)
        StmtPtr stmt;
    };
    std::vector<Handler> handlers;
    std::vector<MsHandler> msHandlers;
    MsExit msExit;
    std::vector<int> indices;
    std::vector<std::string> types;
    bool sawCatchAll = false;

    // **A `throw(T...)` list is a handler of no type of its own**: the table's filter, the selector negative.
    if (!spec.empty()) {
        types.push_back(spec);
        indices.push_back(-1);
    }
    while (spec.empty() && peek().is("catch")) {
        const std::size_t cpos = peek().pos;
        at_++;
        expect("(");
        if (sawCatchAll)
            src_.fail(cpos, "'catch (...)' matches everything, so a handler "
                            "after it could never run");

        Handler h;
        // Where `alive_` stood before the caught object, if it turns out to be
        // one: everything from here is this handler's to destroy, and nothing
        // outside it may see the entry.
        const std::size_t aliveBeforeCaught = alive_.size();
        std::string caughtName;
        const Type *caught = nullptr;      // what the type_info names
        const Type *declaredType = nullptr; // what the handler's own name is
        bool byRef = false;
        bool constReferent = false;
        if (consume("...")) {
            sawCatchAll = true;
        } else {
            StorageClass sc;
            Qualifiers quals;
            const Type *base = specifiers(&sc, &quals);
            Declared d = declarator(base, true);
            // [except.handle]/1: the exception object belongs to the runtime and
            // outlives the handler, so there is nothing here to take apart.
            if (d.type->isRValueReference())
                src_.fail(d.pos, "a handler cannot catch by rvalue reference - "
                                 "the exception object is the runtime's, so "
                                 "catch by value or by 'const &'");
            // **A handler of type `cv T &` matches exactly what `T` matches** - [except.handle]/3 -
            // so the type_info names the referent with its qualifiers off, and the reference is a
            // slot holding the pointer the runtime already has.
            byRef = d.type->isReference();
            constReferent = byRef && d.type->referent()->isConst();
            declaredType = byRef ? d.type : d.type->unqualified();
            caught = byRef ? d.type->referent()->unqualified()
                           : d.type->unqualified();
            // [except.handle]/1: not an incomplete type, an abstract class, nor a pointer or reference to an
            // incomplete type other than cv void *.
            {
                const Type *named = caught->isPointer() ? caught->pointee()->unqualified() : caught;
                if (named->isVoid() && !caught->isPointer())
                    src_.fail(d.pos, "a handler cannot catch 'void' - [except.handle]/1");
                if (!named->isVoid() && !named->isComplete())
                    src_.fail(d.pos, "a handler cannot catch '" + d.type->describe() + "', which names the "
                                     "incomplete type '" + named->describe() + "' - [except.handle]/1");
                if (!byRef && !caught->isPointer() && caught->isStructOrUnion() && caught->abstract())
                    src_.fail(d.pos, "a handler cannot catch the abstract class '" + caught->describe() +
                                     "' by value - [except.handle]/1");
            }
            // The Itanium type_info is emitted by asking for it, so the Microsoft
            // ABI - which names a type descriptor instead - must not ask.
            std::string why;
            if (!microsoft) {
                h.type = typeInfoSymbolFor(caught, cpos, &why);
                if (h.type.empty())
                    src_.fail(cpos, "'catch' cannot name this type: " + why);
            }
            caughtName = d.name;
        }
        expect(")");
        types.push_back(h.type);
        indices.push_back(typeIndexFor(h.type));

        // The handler's own scope, holding the caught object if it was named.
        enterScope();
        const int scope = enterBlock();
        std::vector<StmtPtr> steps;

        // **The Microsoft handler is the block and nothing else.** The runtime has
        // chosen it, made the caught object in the slot the table names, and ends the
        // catch when the funclet returns - so the three Itanium calls are not here.
        if (microsoft) {
            MsHandler mh;
            mh.pos = cpos;
            if (caught != nullptr) {
                MicrosoftThrow names;
                std::string why;
                if (!microsoftThrowNames(caught, caught->size(target_),
                                         &names, &why))
                    src_.fail(cpos, "'catch' cannot name this type: " + why);
                // A type only ever caught still needs its descriptor emitted.
                finishMicrosoftThrow(names, false);
                mh.descriptor = names.descriptor;
                mh.objectSize = caught->size(target_);
                mh.byReference = byRef;
                // `T *const &` is const as cl flags it, the referent's; `const T *&` the pointee's.
                mh.constPointer = names.isConst || (caught->isPointer() && constReferent);
                // [except.handle]/3: a pointer handler matches a thrown nullptr. Microsoft's runtime matches by the
                // type alone, so the table says it: the null is copied into the pointer as it stands.
                if (caught->isPointer() && !byRef) {
                    const Type *nullType = types_.get(Kind::NullPtr);
                    MicrosoftThrow none;
                    if (microsoftThrowNames(nullType, nullType->size(target_), &none, &why)) {
                        finishMicrosoftThrow(none, false);
                        mh.orNullptr = none.descriptor;
                    }
                }
                if (!caughtName.empty()) {
                    mh.objectSlot = declare(caughtName, declaredType, cpos);
                    locals_.back().isCatchParameter = true;
                    // **Caught by value, the runtime built the copy and the handler
                    // destroys it** on every way out - cl's funclet does before returning.
                    if (!byRef && destructorOf(caught->unqualified()) != nullptr)
                        alive_.push_back(Alive{ caughtName, mh.objectSlot, caught->unqualified() });
                }
            }
            if (!peek().is("{")) src_.fail(peek().pos, "'catch' takes a block");
            const bool wasInHandler = inMsHandler_;
            const bool wasBody = inHandlerBody_;
            MsExit *const wasExit = msExit_;
            msExit_ = &msExit;
            inMsHandler_ = true;
            inHandlerBody_ = true;
            // **The same bookkeeping, for the opposite purpose.**
            handlerDepth_++;
            handlerLoopDepth_.push_back(loopDepth_);
            handlerSwitchDepth_.push_back(switchDepth_);
            handlerFrom_.push_back(peek().pos);
            const bool wasCtorHandler = inCtorTryHandler_;
            inCtorTryHandler_ = rethrows;
            mh.body = block();
            inCtorTryHandler_ = wasCtorHandler;
            // [except.handle]/15: flowing off the end of a constructor's or destructor's function-try
            // handler rethrows - from the funclet, as cl ends one.
            if (rethrows) {
                mayThrow_++;
                std::vector<StmtPtr> then;
                then.push_back(std::move(mh.body));
                then.push_back(StmtPtr(new ExprStmt(rethrowCall())));
                mh.body = StmtPtr(new Block(std::move(then)));
            }
            // Falling off the end destroys the caught object; a jump out did for itself.
            if (alive_.size() > aliveBeforeCaught) {
                // **And an exception leaving the handler destroys it too** - a throw from the block, or `throw;`
                // ([except.handle]/16): a state of the funclet, as a local of the block has. Until 1.69 there
                // was none, and `catch (E e) { throw 1; }` left e standing on Windows, where the other targets'
                // pad destroys it.
                std::vector<StmtPtr> guarded;
                guarded.push_back(std::move(mh.body));
                std::vector<StmtPtr> onUnwind;
                emitDestructors(onUnwind, aliveBeforeCaught, cpos);
                StmtPtr covered = cleanupRegion(std::move(guarded), StmtPtr(new Block(std::move(onUnwind))));
                std::vector<StmtPtr> both;
                both.push_back(std::move(covered));
                emitDestructors(both, aliveBeforeCaught, cpos);
                alive_.resize(aliveBeforeCaught);
                Block *b = new Block(std::move(both));
                b->setScope(-1);
                mh.body = StmtPtr(b);
            }
            msExit_ = wasExit;
            handlerFrom_.pop_back();
            handlerSwitchDepth_.pop_back();
            handlerLoopDepth_.pop_back();
            handlerDepth_--;
            inMsHandler_ = wasInHandler;
            inHandlerBody_ = wasBody;
            leaveScope();
            msHandlers.push_back(std::move(mh));
            continue;
        }

        std::vector<ExprPtr> beginArgs;
        ExprPtr ptr(Var::local(".ex.ptr", pointerSlot));
        ptr->setType(voidPtr);
        beginArgs.push_back(std::move(ptr));
        ExprPtr began = runtimeCall("__cxa_begin_catch", voidPtr,
                                    std::move(beginArgs));

        if (caught != nullptr && !caughtName.empty()) {
            const int slot = declare(caughtName, declaredType, cpos);
            locals_.back().isCatchParameter = true;
            // **A reference holds what __cxa_begin_catch handed back.**
            const Type *slotType = byRef
                ? types_.pointerTo(declaredType->referent())
                : types_.pointerTo(caught);
            ExprPtr cast;
            if (byRef && caught->isPointer() && !target_.beginCatchReturnsObject()) {
                // **For a pointer the Itanium runtimes hand back its value, not its object** ([ABI 2.5.3]),
                // so the reference binds to a copy of it held in the frame, as clang's does; TI's hands over
                // the exception object's address, which the reference binds to below like any other.
                const std::string heldName = ".ex.held" + std::to_string(refTemps_++);
                const int heldSlot = allocateFrameSlot(caught);
                ExprPtr held(Var::local(heldName, heldSlot));
                held->setType(caught);
                ExprPtr value(new Cast(caught, std::move(began)));
                value->setType(caught);
                ExprPtr store(new Assign(std::move(held), std::move(value)));
                store->setType(caught);
                steps.push_back(StmtPtr(new ExprStmt(std::move(store))));
                ExprPtr again(Var::local(heldName, heldSlot));
                again->setType(caught);
                cast.reset(new Unary('&', std::move(again)));
                cast->setType(slotType);
            } else if (byRef) {
                cast.reset(new Cast(slotType, std::move(began)));
                cast->setType(slotType);
            }
            if (byRef) {
                ExprPtr to(Var::local(caughtName, slot));
                to->setType(slotType);
                ExprPtr bind(new Assign(std::move(to), std::move(cast)));
                bind->setType(slotType);
                steps.push_back(StmtPtr(new ExprStmt(std::move(bind))));
            } else {
                // **Caught by value, which is a copy-initialisation and was a
                // block copy**.
                const Signature *cc = copyConstructorOf(caught->unqualified());
                ExprPtr fromPtr;
                // **With no `__cxa_get_exception_ptr`** the copy is made from what `__cxa_begin_catch` returns, as cl6x does.
                const bool copyAfterBegin = cc != nullptr && !target_.hasGetExceptionPtr();
                if (cc != nullptr && !copyAfterBegin) {
                    std::vector<ExprPtr> ptrArgs;
                    ExprPtr raw(Var::local(".ex.ptr", pointerSlot));
                    raw->setType(voidPtr);
                    ptrArgs.push_back(std::move(raw));
                    ExprPtr adjusted =
                        runtimeCall("__cxa_get_exception_ptr", voidPtr,
                                    std::move(ptrArgs));
                    fromPtr.reset(new Cast(slotType, std::move(adjusted)));
                } else {
                    fromPtr.reset(new Cast(slotType, std::move(began)));
                }
                fromPtr->setType(slotType);

                if (cc != nullptr) {
                    markUsed(cc);
                    ExprPtr self(Var::local(caughtName, slot));
                    self->setType(caught);
                    ExprPtr at(new Unary('&', std::move(self)));
                    at->setType(types_.pointerTo(caught));
                    ExprPtr source(new Unary('*', std::move(fromPtr)));
                    source->setType(caught);
                    std::vector<ExprPtr> ctorArgs;
                    ctorArgs.push_back(std::move(at));
                    ctorArgs.push_back(std::move(source));
                    std::vector<const Type *> ps;
                    ps.push_back(types_.pointerTo(caught));
                    ps.push_back(cc->params[0]);
                    // **[except.handle]/3: a copy that throws terminates** -
                    // the handler is never entered and nothing propagates.
                    // The block is the terminate scope an unwinding pad gets.
                    std::vector<StmtPtr> copying;
                    copying.push_back(StmtPtr(new ExprStmt(
                        completeCall(caught->unqualified()->tag(), cc->symbol,
                                     nullptr, types_.get(Kind::Void), ps,
                                     false, cpos, std::move(ctorArgs)))));
                    Block *copyBlock = new Block(std::move(copying));
                    copyBlock->setScope(-1);
                    copyBlock->setUnwindCleanup();
                    steps.push_back(StmtPtr(copyBlock));
                } else if (caught->unqualified()->isPointer()) {
                    // **A pointer caught is the pointer __cxa_begin_catch hands back** on the Itanium
                    // runtimes - the value, adjusted to the base; TI's returns the exception object's
                    // address, where the adjusted value lies, so there it is read once more (C1).
                    ExprPtr from;
                    if (target_.beginCatchReturnsObject()) {
                        ExprPtr asObj(new Cast(types_.pointerTo(caught), std::move(fromPtr)));
                        asObj->setType(types_.pointerTo(caught));
                        from.reset(new Unary('*', std::move(asObj)));
                    } else {
                        from.reset(new Cast(caught, std::move(fromPtr)));
                    }
                    from->setType(caught);
                    ExprPtr to(Var::local(caughtName, slot));
                    to->setType(caught);
                    ExprPtr copy(new Assign(std::move(to), std::move(from)));
                    copy->setType(caught);
                    steps.push_back(StmtPtr(new ExprStmt(std::move(copy))));
                } else {
                    // No copy constructor is a trivially copyable class, or a
                    // fundamental type, and the bytes are the copy.
                    ExprPtr from(new Unary('*', std::move(fromPtr)));
                    from->setType(caught);
                    ExprPtr to(Var::local(caughtName, slot));
                    to->setType(caught);
                    ExprPtr copy(new Assign(std::move(to), std::move(from)));
                    copy->setType(caught);
                    steps.push_back(StmtPtr(new ExprStmt(std::move(copy))));
                }
                // **The catch is entered after the copy** where a constructor
                // ran, which is the order the two calls exist to make
                // possible.
                if (cc != nullptr && !copyAfterBegin)
                    steps.push_back(StmtPtr(new ExprStmt(std::move(began))));
                // **An object of this scope from here on.**
                if (destructorOf(caught->unqualified()) != nullptr)
                    alive_.push_back(Alive{ caughtName, slot,
                                            caught->unqualified() });
            }
        } else {
            steps.push_back(StmtPtr(new ExprStmt(std::move(began))));
        }

        if (!peek().is("{")) src_.fail(peek().pos, "'catch' takes a block");
        const bool wasBody = inHandlerBody_;
        inHandlerBody_ = true;
        // **What a jump out of this handler has to end**, and where the loops
        // around it stood when it was entered - see endCatches().
        handlerDepth_++;
        handlerLoopDepth_.push_back(loopDepth_);
        handlerSwitchDepth_.push_back(switchDepth_);
        handlerFrom_.push_back(peek().pos);
        // **Whether anything in this handler could throw**, as noexcept counts.
        const int throwsBefore = mayThrow_;
        // **Named before the block is read**, because a region inside it hands
        // over to this pad and has to know where that is. The `$` keeps it out
        // of reach of any label a program can write, the way `$chain` does.
        const std::string endCatchLabel =
            "$endcatch" + std::to_string(refTemps_++);
        int padPtr = 0, padSel = 0;
        const std::string beyond = unwindTarget(&padPtr, &padSel);
        if (beyond.empty()) {
            padPtr = allocateFrameSlot(voidPtr);
            padSel = allocateFrameSlot(types_.intType());
        }
        const std::string wasHandlerLabel2 = handlerResumeLabel_;
        const int wasHandlerPtr2 = handlerResumePtr_;
        const int wasHandlerSel2 = handlerResumeSel_;
        const std::size_t wasHandlerAliveFrom2 = handlerAliveFrom_;
        if (!microsoft) {
            handlerResumeLabel_ = endCatchLabel;
            handlerResumePtr_ = padPtr;
            handlerResumeSel_ = padSel;
            handlerAliveFrom_ = alive_.size();       // what the handler's block builds from here is its pads' to destroy
        }
        const bool wasCtorHandler = inCtorTryHandler_;
        inCtorTryHandler_ = rethrows;
        const int askedBefore = unwindTargetsAsked_;
        StmtPtr handlerBody = block();
        // A local with a destructor declared in the handler named this pad in its cleanup row, whether or
        // not anything after it can throw: the pad is defined then (e20: `catch (int) { G g; return 0; }`).
        const bool padNamed = unwindTargetsAsked_ > askedBefore;
        inCtorTryHandler_ = wasCtorHandler;
        // [except.handle]/15: flowing off the end of a constructor's function-try handler rethrows.
        if (rethrows) {
            mayThrow_++;
            std::vector<StmtPtr> then;
            then.push_back(std::move(handlerBody));
            then.push_back(StmtPtr(new ExprStmt(runtimeCall("__cxa_rethrow", types_.get(Kind::Void),
                                                            std::vector<ExprPtr>()))));
            handlerBody = StmtPtr(new Block(std::move(then)));
        }
        handlerResumeLabel_ = wasHandlerLabel2;
        handlerResumePtr_ = wasHandlerPtr2;
        handlerResumeSel_ = wasHandlerSel2;
        handlerAliveFrom_ = wasHandlerAliveFrom2;
        const bool canThrow = mayThrow_ > throwsBefore || padNamed;
        handlerFrom_.pop_back();
        handlerSwitchDepth_.pop_back();
        handlerLoopDepth_.pop_back();
        handlerDepth_--;
        inHandlerBody_ = wasBody;

        // **An exception leaving the handler has to end the catch too**, and
        // it is the one way out no jump can be written for: the unwinder takes
        // it.
        if (canThrow && !microsoft) {
            std::vector<StmtPtr> guarded;
            guarded.push_back(std::move(handlerBody));
            std::vector<StmtPtr> padSteps;
            // **The by-value parameter goes first**, being an object of the
            // handler's own scope: [except.handle]/16 destroys those and then
            // ends the handling, whichever way the handler is left.
            emitDestructors(padSteps, aliveBeforeCaught, cpos);
            padSteps.push_back(StmtPtr(new ExprStmt(
                runtimeCall("__cxa_end_catch", types_.get(Kind::Void),
                            std::vector<ExprPtr>()))));
            // **And the objects outside the `try`, where this pad is the last
            // one in the frame.**
            if (aliveOutside > bodyCleanupFrom_ && beyond.empty())
                emitDestructors(padSteps, bodyCleanupFrom_, pos, -1,
                                aliveOutside);
            // **Handed to the enclosing `try`'s chain**, past the rows between:
            // what its body built before this `try` is destroyed here first.
            if (!beyond.empty() && beyond == wasChain && aliveOutside > wasAliveFrom)
                emitDestructors(padSteps, wasAliveFrom, pos, -1, aliveOutside);
            // **Or to the pad of the handler this `try` sits in**: what that handler's block built before this
            // `try` is destroyed here first, for the same reason (1.69; the suite's eh05, whose `b` stood).
            if (!beyond.empty() && beyond == wasHandlerLabel && aliveOutside > handlerAliveFrom_)
                emitDestructors(padSteps, handlerAliveFrom_, pos, -1, aliveOutside);
            if (!beyond.empty()) {
                padSteps.push_back(StmtPtr(new Goto(beyond)));
            } else {
                padSteps.push_back(resumeUnwinding(padPtr));
            }
            // Behind the label a region inside this handler jumps to.
            StmtPtr padLabelled(new Label(endCatchLabel, unwindPad(std::move(padSteps))));
            Try *region = new Try(std::move(guarded), std::move(padLabelled),
                                  padPtr, padSel, std::vector<std::string>());
            // **Only where the row will carry types.**
            if (!enclosingChain.empty()) region->setAlsoCleanup();
            steps.push_back(StmtPtr(region));
        } else {
            steps.push_back(std::move(handlerBody));
        }

        // Falling off the end of the block, which is the way out that always
        // reached this call; every other way out makes it for itself now.
        emitDestructors(steps, aliveBeforeCaught, cpos);
        ExprPtr ended = runtimeCall("__cxa_end_catch", types_.get(Kind::Void),
                                    std::vector<ExprPtr>());
        steps.push_back(StmtPtr(new ExprStmt(std::move(ended))));
        // **And it is gone from here on.** Left in `alive_` it would be
        // destroyed again by the block around the `try`, and every jump past
        // this point would carry it too.
        alive_.resize(aliveBeforeCaught);
        leaveScope();
        Block *b = new Block(std::move(steps));
        b->setScope(scope);
        h.stmt = StmtPtr(b);
        handlers.push_back(std::move(h));
    }

    // **Microsoft: a `throw(T...)` list is handlers that rethrow what it allows, and a catch-all calling
    // unexpected** (Phase 6): what that throws goes on where the list allows it, is std::bad_exception where
    // the list names that, and ends the program otherwise - [except.unexpected]. cl ignores the list.
    if (microsoft && !spec.empty()) {
        std::vector<StmtPtr> call;
        call.push_back(StmtPtr(new ExprStmt(runtimeCall("unexpected", types_.get(Kind::Void), std::vector<ExprPtr>()))));
        const Signature *makeBad = nullptr;
        const Type *bad = nullptr;
        for (const Type *t : *allowed)
            if (t->isStructOrUnion() && t->tag() == "std::bad_exception" && defaultConstructorOf(t) != nullptr) {
                bad = t;
                makeBad = defaultConstructorOf(t);
            }
        StmtPtr otherwise = bad != nullptr
            ? microsoftThrow(constructTemporary(bad, *makeBad, std::vector<ExprPtr>(), false, pos), pos)
            : StmtPtr(new ExprStmt(runtimeCall("terminate", types_.get(Kind::Void), std::vector<ExprPtr>())));
        Try *inner = new Try(std::move(call), nullptr, allocateFrameSlot(voidPtr),
                             allocateFrameSlot(types_.intType()), std::vector<std::string>(allowed->size() + 1));
        inner->setHandlers(msSpecHandlers(*allowed, std::move(otherwise), pos));
        inner->setUnwindHelpSlot(msUnwindHelp());
        msHandlers = msSpecHandlers(*allowed, StmtPtr(inner), pos);
        types.assign(msHandlers.size(), std::string());
    }

    // Microsoft: no chain to build, because nothing in this frame chooses.
    if (microsoft) {
        std::vector<StmtPtr> guardedMs;
        guardedMs.push_back(std::move(body));
        Try *t = new Try(std::move(guardedMs), nullptr, pointerSlot,
                         selectorSlot, std::move(types));
        t->setHandlers(std::move(msHandlers));
        // The runtime's scratch word, which the personality routine finds through the
        // FuncInfo's dispUnwindHelp and the parent sets to -2 on entry. A frame slot
        // like any other, so where it lives is decided where every local's is.
        t->setUnwindHelpSlot(msUnwindHelp());
        std::vector<std::pair<std::string, std::string> > gotoExits;
        // A goto to a label inside this `try` statement never leaves its funclet, so it needs no exit.
        for (const std::pair<std::string, std::string> &e : msExit.gotos) {
            bool inside = false;
            for (const LabelDef &l : labels_) inside = inside || (l.name == e.second && l.pos >= pos);
            if (!inside) gotoExits.push_back(e);
        }
        if (msExit.ret.empty() && msExit.brk.empty() && msExit.cont.empty() && gotoExits.empty())
            return StmtPtr(t);
        std::vector<std::string> exits;
        for (const std::string *e : { &msExit.ret, &msExit.brk, &msExit.cont })
            if (!e->empty()) exits.push_back(*e);
        for (const std::pair<std::string, std::string> &e : gotoExits) exits.push_back(e.first);
        t->setMsExits(std::move(exits));
        // **Where a handler's early exit carries on**: the funclet handed back one
        // of these labels, and the parent does here what the handler could not.
        std::vector<StmtPtr> after;
        after.push_back(StmtPtr(t));
        std::string skip;
        after.push_back(StmtPtr(new Goto(msExitLabel(skip, "skip"))));
        // **Inside a handler this is a funclet too**, and an exit that leaves it hands on to the exit
        // of the `try` around - the handler's funclet destroyed what the jump left, both handlers' share.
        const bool chains = inMsHandler_ && msExit_ != nullptr;
        if (!msExit.ret.empty()) {
            ExprPtr give;
            if (msExit.retSlot != 0) {
                give.reset(Var::local(msExit.retTemp, msExit.retSlot));
                give->setType(returnSlotType());
            }
            StmtPtr then;
            if (chains) {
                std::vector<StmtPtr> pass;
                if (give != nullptr) {
                    if (msExit_->retSlot == 0) {
                        msExit_->retSlot = allocateFrameSlot(returnSlotType());
                        msExit_->retTemp = ".ret" + std::to_string(refTemps_++);
                    }
                    ExprPtr keep(Var::local(msExit_->retTemp, msExit_->retSlot));
                    keep->setType(returnSlotType());
                    ExprPtr save(new Assign(std::move(keep), std::move(give)));
                    save->setType(returnSlotType());
                    pass.push_back(StmtPtr(new ExprStmt(std::move(save))));
                }
                pass.push_back(StmtPtr(new FuncletLeave(msExitLabel(msExit_->ret, "ret"))));
                then.reset(new Block(std::move(pass)));
            } else {
                then = give != nullptr ? StmtPtr(new Return(std::move(give))) : leaveBody();
            }
            after.push_back(StmtPtr(new Label(msExit.ret, std::move(then))));
        }
        if (!msExit.brk.empty())
            after.push_back(StmtPtr(new Label(msExit.brk, chains && handlersLeftByBreak() > 0
                ? StmtPtr(new FuncletLeave(msExitLabel(msExit_->brk, "brk"))) : StmtPtr(new Break()))));
        if (!msExit.cont.empty())
            after.push_back(StmtPtr(new Label(msExit.cont, chains && handlersLeftByContinue() > 0
                ? StmtPtr(new FuncletLeave(msExitLabel(msExit_->cont, "cont"))) : StmtPtr(new Continue()))));
        // The funclet destroyed what the jump left, so the parent's half only jumps on.
        for (const std::pair<std::string, std::string> &e : gotoExits)
            after.push_back(StmtPtr(new Label(e.first, gotoStatement(e.second, pos, false))));
        after.push_back(StmtPtr(new Label(skip, StmtPtr(new Block({})))));
        Block *b = new Block(std::move(after));
        b->setScope(-1);
        return StmtPtr(b);
    }

    // **Nothing matched, so this frame unwinds like any other.**
    std::vector<StmtPtr> resume;
    // **A nested `try` hands over instead of resuming.**
    int outPtr = 0, outSel = 0;
    const std::string beyondTry = unwindTarget(&outPtr, &outSel);
    const bool unwindsHere = aliveOutside > bodyCleanupFrom_ &&
                             beyondTry.empty();
    if (unwindsHere) emitDestructors(resume, bodyCleanupFrom_, pos,
                                     -1, aliveOutside);
    // **Handing to the enclosing `try`'s chain skips the rows between**, so
    // what its body built before this `try` is destroyed here first.
    if (!beyondTry.empty() && beyondTry == wasChain && aliveOutside > wasAliveFrom)
        emitDestructors(resume, wasAliveFrom, pos, -1, aliveOutside);
    // the same where it is the pad of the handler this `try` sits in that takes over (1.69)
    if (!beyondTry.empty() && beyondTry == wasHandlerLabel && aliveOutside > handlerAliveFrom_)
        emitDestructors(resume, handlerAliveFrom_, pos, -1, aliveOutside);
    if (beyondTry.empty()) resume.push_back(resumeUnwinding(pointerSlot));
    else                   resume.push_back(StmtPtr(new Goto(beyondTry)));
    StmtPtr chain = unwindPad(std::move(resume));
    // [except.unexpected]: an exception the list does not allow, once this frame is unwound.
    if (!spec.empty()) {
        std::vector<ExprPtr> args;
        ExprPtr ptr(Var::local(".ex.ptr", pointerSlot));
        ptr->setType(voidPtr);
        args.push_back(std::move(ptr));
        StmtPtr unexpected(new ExprStmt(runtimeCall("__cxa_call_unexpected", types_.get(Kind::Void), std::move(args))));
        ExprPtr sel(Var::local(".ex.sel", selectorSlot));
        sel->setType(types_.intType());
        ExprPtr zero(new Num(0LL));
        zero->setType(types_.intType());
        ExprPtr test(new Binary(BinOp::Lt, std::move(sel), std::move(zero)));
        test->setType(types_.intType());
        chain = StmtPtr(new If(std::move(test), std::move(unexpected), std::move(chain)));
    }

    for (std::size_t i = handlers.size(); i-- > 0; ) {
        if (handlers[i].type.empty()) {          // catch (...) matches always
            chain = std::move(handlers[i].stmt);
            continue;
        }
        ExprPtr sel(Var::local(".ex.sel", selectorSlot));
        sel->setType(types_.intType());
        ExprPtr want(new Num(static_cast<long long>(indices[i])));
        want->setType(types_.intType());
        ExprPtr test(new Binary(BinOp::Eq, std::move(sel), std::move(want)));
        test->setType(types_.intType());
        chain = StmtPtr(new If(std::move(test), std::move(handlers[i].stmt),
                               std::move(chain)));
    }

    // **Every segment of the body carries this `try`'s catch types.**
    for (std::size_t i = 0; i < segments.size(); i++) {
        segments[i]->setTypes(types);
        segments[i]->setTypeIndices(indices);
        segments[i]->setAlsoCleanup();
    }
    // The one chain, behind the label those segments jump to.
    StmtPtr labelled(new Label(chainLabel, std::move(chain)));

    std::vector<StmtPtr> guarded;
    guarded.push_back(std::move(body));
    Try *t = new Try(std::move(guarded), std::move(labelled), pointerSlot,
                     selectorSlot, std::move(types));
    t->setTypeIndices(indices);
    // The call site needs a trailing filter-0 action, or phase 2 installs no
    // pad where no handler matched and these destructors never run.
    if (unwindsHere) t->setAlsoCleanup();
    return StmtPtr(t);
}

void Parser::enterBlockEntry(std::map<std::string, std::string> &table, const std::string &key,
                             const std::string &value) {
    std::map<std::string, std::string>::const_iterator had = table.find(key);
    blockUndo_.push_back(BlockUndo{ &table, key, had != table.end() ? had->second : std::string() });
    table[key] = value;
}

// `using N::f;` in a block - [namespace.udecl]/1: `f` is N's to the block's
// '}'; a function's name joins the block's using-declarations (ADL stays),
// any other later `f` is rewritten to N's, until a nested `f` hides it.
void Parser::blockUsingDeclaration() {
    const std::size_t pos = peek().pos;
    at_++;
    const bool fromGlobal = consume("::");
    std::string target;
    for (;;) {
        if (peek().is("operator")) {
            at_++;
            if (peek().kind == TokenKind::Str) {
                std::string suffix = peek().udSuffix;
                at_++;
                if (suffix.empty()) suffix = expectIdent("a literal operator's suffix");
                target += "operator\"\" " + suffix;
            } else if (peek().is("(") && peekAt(1).is(")")) {
                at_ += 2;
                target += "operator()";
            } else if (peek().is("[") && peekAt(1).is("]")) {
                at_ += 2;
                target += "operator[]";
            } else if (peek().kind == TokenKind::Punct) {
                target += "operator" + peek().text;
                at_++;
            } else {
                src_.fail(peek().pos, "expected an operator after 'operator' in this using-declaration");
            }
            break;
        }
        target += expectIdent("a name after 'using'");
        if (!consume("::")) break;
        target += "::";
    }
    expect(";");
    const std::string::size_type cut = target.rfind("::");
    if (!fromGlobal && cut == std::string::npos)
        src_.fail(pos, "a using-declaration names something declared elsewhere, so it takes a "
                       "qualified name: 'using N::f;', or 'using ::f;' for one at global scope");
    const std::string written = target;
    if (cut != std::string::npos) {
        const Type *owner = findTypedef(written.substr(0, cut));
        if (owner != nullptr && owner->unqualified()->isStructOrUnion())
            src_.fail(pos, "'" + written + "' is a member of a class, and a using-declaration in a block "
                           "names a namespace's member only - [namespace.udecl]/8");
    }
    if (!fromGlobal && cut != std::string::npos) {
        const std::string::size_type first = target.find("::");
        const std::string scope = namespaceNamed(target.substr(0, first));
        if (!scope.empty()) target = scope + target.substr(first);
    }
    target = followUsingDeclaration(target);
    bool fnTemplate = false;
    {
        const std::string::size_type c = target.rfind("::");
        std::map<std::string, std::vector<TemplateDecl> >::const_iterator ft =
            fnTemplates_.find(c == std::string::npos ? target : target.substr(c + 2));
        const std::string ns = c == std::string::npos ? std::string() : target.substr(0, c + 2);
        if (ft != fnTemplates_.end())
            for (const TemplateDecl &t : ft->second) fnTemplate = fnTemplate || t.ns == ns;
    }
    const std::map<std::string, TemplateDecl>::const_iterator tmplNamed = findTemplate(target);
    const bool classTemplate = tmplNamed != templates_.end() && tmplNamed->second.isClass && cut != std::string::npos &&
                               tmplNamed->second.ns == target.substr(0, target.rfind("::") + 2);
    if (classTemplate) enterBlockEntry(templateUsing_, namespacePrefix() + target.substr(target.rfind("::") + 2), tmplNamed->first);
    if (!hasTypeNamed(target) && !hasFunctionNamed(target) && !hasGlobalNamed(target) &&
        !hasEnumNamed(target) && !isTemplateName(target) && !fnTemplate && !classTemplate)
        src_.fail(pos, "'" + written + "' is not declared, so there is nothing here for this "
                       "using-declaration to name");
    const std::string name = cut == std::string::npos ? written : written.substr(cut + 2);
    if ((hasFunctionNamed(target) || fnTemplate) && !hasTypeNamed(target) && !hasGlobalNamed(target))
        enterBlockEntry(blockUsingFunctions_, name, target);
    if (target == name || classTemplate) return;
    if ((hasFunctionNamed(target) || fnTemplate) && !hasTypeNamed(target) && !hasGlobalNamed(target)) {
        enterBlockEntry(usingDeclarations_, namespacePrefix() + name, target);
        return;
    }

    static const char *const typeWords[] = { "int", "char", "bool", "short", "long", "signed", "unsigned",
                                             "float", "double", "void", "auto", "wchar_t", "char16_t",
                                             "char32_t", "const", "volatile" };
    auto typeEnds = [&](std::size_t k) {
        if (tokens_[k].kind == TokenKind::Ident || tokens_[k].is(">")) return true;
        for (const char *w : typeWords) if (tokens_[k].is(w)) return true;
        return false;
    };
    auto startsStatement = [&](std::size_t k) {
        return tokens_[k].is(";") || tokens_[k].is("{") || tokens_[k].is("}");
    };
    // `T f`, `T *f`, `T &f` followed by what a declarator ends with: a declaration of `f` there.
    auto declaresAt = [&](std::size_t i) {
        const Token &next = tokens_[i + 1];
        if (!(next.is("=") || next.is(";") || next.is(",") || next.is("[") || next.is("{") ||
              next.is("(") || next.is(")")))
            return false;
        if (typeEnds(i - 1)) return true;
        if (!(tokens_[i - 1].is("*") || tokens_[i - 1].is("&") || tokens_[i - 1].is("&&")) || i < 3) return false;
        std::size_t k = i - 2;
        if (!typeEnds(k)) return false;
        while (k > 0 && (typeEnds(k) || tokens_[k].is("::"))) k--;
        return startsStatement(k);
    };
    int depth = 0, hidden = -1;
    for (std::size_t i = at_; i + 1 < tokens_.size() && tokens_[i].kind != TokenKind::End; i++) {
        if (tokens_[i].is("{")) { depth++; continue; }
        if (tokens_[i].is("}")) {
            if (--depth < 0) break;
            if (hidden >= 0 && depth < hidden) hidden = -1;
            continue;
        }
        if (hidden >= 0 || tokens_[i].kind != TokenKind::Ident || tokens_[i].text != name) continue;
        const Token &before = tokens_[i - 1];
        if (before.is("::") || before.is(".") || before.is("->") || before.is("~") || before.is("goto") ||
            before.is("struct") || before.is("class") || before.is("union") || before.is("enum"))
            continue;
        if (declaresAt(i)) {
            if (depth == 0) break;
            hidden = depth;
            continue;
        }
        tokens_[i].text = target;
    }
}

StmtPtr Parser::statementBody() {
    // A static_assert declares nothing, so the statement it becomes is empty.
    if (staticAssertion()) return declarativeStatement();
    if (attributedDeclarationAhead()) return declaration();

    // **`using namespace N;` inside a block**, the same directive as the one at file
    // scope, differing only in when it stops applying: at the end of this block, which
    // `block()` undoes by truncating the list. It becomes the empty statement.
    if (peek().is("using") && peekAt(1).is("namespace")) {
        at_ += 2;
        std::string opened = expectIdent("a namespace name");
        while (peek().is("::")) {
            at_++;
            opened += "::" + expectIdent("a namespace name");
        }
        expect(";");
        // The alias's own name is what a namespace alias stands for, [namespace.alias]/2.
        const std::string resolved = resolveNamespaceName(opened);
        if (resolved.empty())
            src_.fail(peek().pos, "'" + opened + "' is not a namespace");
        usingNamespaces_.push_back(resolved);
        return declarativeStatement();
    }
    // [namespace.alias] in a block: the alias's reach is the rest of the block, [basic.scope.block].
    if (peek().is("namespace") && peekAt(1).kind == TokenKind::Ident && peekAt(2).is("=")) {
        const std::size_t pos = peek().pos;
        const std::string name = peekAt(1).text;
        at_ += 3;
        namespaceAlias(name, pos, true);
        return declarativeStatement();
    }

    if (aliasDeclaration("")) return declarativeStatement();
    if (peek().is("using")) {
        blockUsingDeclaration();
        return declarativeStatement();
    }

    if (peek().is("try")) {
        const std::size_t tpos = peek().pos;
        at_++;
        return tryStatement(tpos);
    }

    if (peek().is("throw")) {
        const std::size_t tpos = peek().pos;
        at_++;
        // **A rethrow hands back the exception the handler is holding**, and
        // the runtime is the one that knows which - so there is nothing to
        // name and nothing to allocate.
        if (peek().is(";")) {
            at_++;
            // [except.throw]/8: with no exception being handled this calls
            // std::terminate, which is what both runtimes do by themselves -
            // so it is well-formed here, as clang has it, and needs no check.
            mayThrow_++;
            return StmtPtr(new ExprStmt(rethrowCall()));
        }
        mayThrow_++;
        ExprPtr value = decay(expr());
        checkThrowOperand(*value, tpos);
        // **A thrown expression's temporary is destroyed by the throw itself.**
        expect(";");
        // **[except.spec]/9: an exception that escapes a `noexcept` function
        // calls std::terminate.**
        if (inNoexceptFunction_ && !inTryBody_ && !inHandlerBody_) {
            std::vector<StmtPtr> both;
            both.push_back(StmtPtr(new ExprStmt(std::move(value))));
            both.push_back(StmtPtr(new ExprStmt(terminateCall())));
            return StmtPtr(new Block(std::move(both)));
        }
        return throwStatement(std::move(value), tpos);
    }

    // **A `return` inside a Microsoft handler leaves the funclet**, the value
    // saved in the parent's frame and the parent returning it at the label.
    const bool leavesFunclet = inMsHandler_ && msExit_ != nullptr &&
                               deducingReturn_ == nullptr;
    if (peek().is("return") && inCtorTryHandler_)
        src_.fail(peek().pos, "a handler of a constructor's function try block cannot 'return' - "
                              "[except.handle]/13; it ends by rethrowing, or by a 'throw' of its own");
    if (consume("return")) {
        std::size_t pos = peek().pos;
        // **A lambda's body, read to find what it returns**: the operand's
        // type is the answer, the first `return` deciding, and the statement
        // built here is thrown away with the rest of that reading.
        if (deducingReturn_ != nullptr) {
            const Type *found = types_.get(Kind::Void);
            if (!consume(";")) {
                ExprPtr value = decay(expr());
                if (value != nullptr && value->type() != nullptr)
                    found = decayedType(value->type());
                expect(";");
            }
            // [expr.prim.lambda]/4 as CWG 975 gives it to C++11: every return deduces the one type, a
            // scalar's cv dropped by the lvalue-to-rvalue conversion.
            auto deduced = [](const Type *t) { return t->unqualified()->isStructOrUnion() ? t : t->unqualified(); };
            if (*deducingReturn_ != nullptr && deduced(*deducingReturn_) != deduced(found))
                src_.fail(pos, "return type '" + found->describe() + "' must match previous return type '" +
                               (*deducingReturn_)->describe() + "' when lambda expression has unspecified "
                               "explicit return type");
            if (*deducingReturn_ == nullptr) *deducingReturn_ = found;
            return StmtPtr(new Return(nullptr));
        }
        if (consume(";")) {
            if (!returnType_->isVoid())
                src_.fail(pos, "this function returns '" + returnType_->describe() +
                               "', so 'return' needs a value - a bare 'return' is "
                               "only for a function returning 'void'");
            if (!alive_.empty() || handlerDepth_ > 0) {
                std::vector<StmtPtr> unwind;
                emitDestructors(unwind, 0, pos);
                endCatches(unwind, handlerDepth_);
                if (leavesFunclet)
                    unwind.push_back(StmtPtr(new FuncletLeave(msExitLabel(msExit_->ret, "ret"))));
                else
                    unwind.push_back(leaveBody());
                return StmtPtr(new Block(std::move(unwind)));
            }
            return leaveBody();
        }
        ExprPtr returned;
        {
            OverloadTarget hold(*this, returnType_);
            // [stmt.return]/2: `return {4, 5};` copy-list-initialises the returned object from the
            // braces, [dcl.init.list] (1.27, 2026-10-21, review of 1.25, item 3 - "expected an
            // expression").
            if (peek().is("{") && !returnType_->isReference() && !returnType_->isVoid()) {
                Init in = parseInitialiser();
                returned = listTemporaryFrom(returnType_, in, true, pos);
            } else
                returned = returnType_->isReference() ? expr() : decay(expr());
        }
        // **The temporary that *is* the returned value must not be destroyed
        // here.**
        if (!returnType_->isReference())
            if (ExprPtr made = userConversion(returnType_, returned, pos))
                returned = std::move(made);
        if (!returnType_->isReference() && returnType_->isStructOrUnion() && sretSlot_ != 0 &&
            returnType_->unqualified()->nonTrivialCopy() &&
            (copyConstructorOf(returnType_->unqualified()) != nullptr ||
             moveConstructorOf(returnType_->unqualified()) != nullptr))
            return returnInPlace(std::move(returned), pos, leavesFunclet);
        bool ownedTemporary = false;
        if (!returnType_->isReference() && returnType_->isStructOrUnion()) {
            // A temporary of the returned class is the returned object whether or not the class has a
            // destructor (1.78: only one with a destructor was, and `return M(a);` of another called M's move).
            const bool fresh = freshTemporarySlot(*returned, returnType_) != 0;
            ownedTemporary = releaseTemporary(*returned) || fresh;
        }
        // **A reference is bound before the full expression ends**, so what outlives its temporaries is the
        // address: `return os << v.str();` kept a copy of the ostream and returned the copy's.
        ExprPtr value;
        bool returnedParameter = false;
        if (returnType_->isReference()) {
            value = bindReference(returnType_, std::move(returned), pos,
                                  "this function's return type");
            if (dynamic_cast<const Comma *>(value.get()) != nullptr)
                src_.fail(pos, "this returns a reference to a temporary of "
                               "this function, which is gone by the time the "
                               "caller could read it");
            value = endFullExpression(std::move(value));
        } else {
            value = endFullExpression(std::move(returned));
            // [stmt.return]/2 copy-initialises the returned object, converting.
            checkAssignable(*value, returnType_, pos, "this function's return type");
            // Does the operand name a by-value parameter of this function? One that
            // arrived by address was lowered to a reference and reads back as `*slot`;
            // `byValueByAddress` is what tells it from a genuine `T &t` or `*p`.
            {
                const Expr *named = value.get();
                bool viaDeref = false;
                if (const Unary *u = dynamic_cast<const Unary *>(named))
                    if (u->op() == '*') { named = &u->operand(); viaDeref = true; }
                if (const Var *v = dynamic_cast<const Var *>(named))
                    if (v->isLocal())
                        if (const Local *l = findLocal(v->name()))
                            if (l->isParameter)
                                returnedParameter = viaDeref
                                                  ? l->byValueByAddress
                                                  : !l->type->isReference();
            }
            // **[class.copy]/31 again: returning by value copy-initializes the caller's
            // object**, so its copy constructor is selected and checked though the copy
            // is elided below. An explicit one makes the function ill-formed alone.
            if (returnType_->isStructOrUnion() && value->type() != nullptr &&
                value->type()->unqualified() == returnType_->unqualified()) {
                // **Which constructor `return` selects is decided rvalue-first for an
                // automatic object** - [class.copy]/32: resolution runs first as if the
                // operand were an rvalue, and only then as the lvalue it is.
                bool asRvalue = !isGlvalue(*value) || value->isXvalue() ||
                                returnedParameter;
                if (const Var *v = dynamic_cast<const Var *>(value.get()))
                    if (v->isLocal()) asRvalue = true;
                const Signature *mc = moveConstructorOf(returnType_->unqualified());
                const Signature *sel = asRvalue && mc != nullptr
                                     ? mc
                                     : copyConstructorOf(returnType_->unqualified());
                if (sel != nullptr && sel->isExplicit)
                    src_.fail(pos, "'" + returnType_->describe() + "' has an "
                                   "'explicit' " +
                                   (sel == mc ? "move" : "copy") +
                                   " constructor, so it "
                                   "cannot be returned by value - 'return' "
                                   "copy-initialises the caller's object, "
                                   "and that may not pick an explicit "
                                   "constructor even where the copy is "
                                   "elided");
            }
            value = convert(std::move(value), returnType_);
        }
        expect(";");

        // **A return runs every destructor the function still owes, and the value is
        // computed first**, into a slot of its own. **What is returned is not destroyed
        // here** - that is elision, and [class.copy]/31 excludes a parameter from it.
        bool elidable = false;
        if (const Var *v = dynamic_cast<const Var *>(value.get()))
            if (v->isLocal()) {
                const Local *l = findLocal(v->name());
                elidable = l == nullptr || !l->isParameter;
            }

        // **A `return` of a glvalue this function does not own has to call the copy
        // constructor**, and nothing did: the byte move let the destructor's elision
        // stand in. The copy is built into a slot of this frame, and that is elided.
        std::vector<StmtPtr> before;
        if (returnType_->isStructOrUnion() && !elidable && !ownedTemporary &&
            value->type() != nullptr &&
            value->type()->unqualified() == returnType_->unqualified() &&
            isGlvalue(*value) &&
            (copyConstructorOf(returnType_->unqualified()) != nullptr ||
             moveConstructorOf(returnType_->unqualified()) != nullptr)) {
            Declared rv;
            rv.name = ".rv" + std::to_string(refTemps_++);
            rv.type = returnType_;
            rv.pos = pos;
            const int slot = declare(rv.name, rv.type, pos);
            // **A returned parameter moves.** [class.copy]/32 again, on the copy that
            // is actually built: the operand is designated an rvalue first, so a
            // move-only class can be returned by value at all, as C++11 promises.
            if (returnedParameter &&
                moveConstructorOf(returnType_->unqualified()) != nullptr)
                value->setXvalue();
            std::vector<ExprPtr> one;
            one.push_back(std::move(value));
            before.push_back(constructLocal(rv, slot, std::move(one), true));
            ExprPtr built(Var::local(rv.name, slot));
            built->setType(returnType_);
            value = std::move(built);
            elidable = true;
        }

        int elided = -1;
        if (returnType_->isStructOrUnion() &&
            destructorOf(returnType_) != nullptr && elidable)
            if (const Var *v = dynamic_cast<const Var *>(value.get()))
                if (v->isLocal()) elided = v->offset();

        if (!alive_.empty() || handlerDepth_ > 0) {
            std::vector<StmtPtr> unwind;
            for (std::size_t i = 0; i < before.size(); i++)
                unwind.push_back(std::move(before[i]));
            // **A void function returning a void expression has nothing to
            // save**, and asking for a frame slot of `void` is the wrong
            // question. It only arrives here at all because a handler is open.
            if (returnType_->isVoid()) {
                unwind.push_back(StmtPtr(new ExprStmt(std::move(value))));
                emitDestructors(unwind, 0, pos, elided);
                endCatches(unwind, handlerDepth_);
                if (leavesFunclet)
                    unwind.push_back(StmtPtr(new FuncletLeave(msExitLabel(msExit_->ret, "ret"))));
                else
                    unwind.push_back(StmtPtr(new Return(nullptr)));
                return StmtPtr(new Block(std::move(unwind)));
            }
            // **The value is computed into a slot before the catch ends**, and
            // that is not only tidiness: `return e.v;` reads the caught object,
            // which `__cxa_end_catch` destroys.
            int slot;
            std::string temp;
            if (leavesFunclet) {
                // One slot per `try`, since one label in the parent reads it.
                if (msExit_->retSlot == 0) {
                    msExit_->retSlot = allocateFrameSlot(returnSlotType());
                    msExit_->retTemp = ".ret" + std::to_string(refTemps_++);
                }
                slot = msExit_->retSlot;
                temp = msExit_->retTemp;
            } else {
                slot = allocateFrameSlot(returnSlotType());
                temp = ".ret" + std::to_string(refTemps_++);
            }

            // A reference return keeps the address bindReference made - saved as the referent, `return os << s;`
            // copied the whole ostream into the frame and returned the copy's address.
            ExprPtr keep(Var::local(temp, slot));
            keep->setType(returnSlotType());
            ExprPtr save(new Assign(std::move(keep), std::move(value)));
            save->setType(returnSlotType());
            unwind.push_back(StmtPtr(new ExprStmt(std::move(save))));

            emitDestructors(unwind, 0, pos, elided);
            endCatches(unwind, handlerDepth_);

            if (leavesFunclet) {
                unwind.push_back(StmtPtr(new FuncletLeave(msExitLabel(msExit_->ret, "ret"))));
                return StmtPtr(new Block(std::move(unwind)));
            }
            ExprPtr give(Var::local(temp, slot));
            give->setType(returnSlotType());
            unwind.push_back(StmtPtr(new Return(std::move(give))));
            return StmtPtr(new Block(std::move(unwind)));
        }
        if (!before.empty()) {
            before.push_back(StmtPtr(new Return(std::move(value))));
            return StmtPtr(new Block(std::move(before)));
        }
        return StmtPtr(new Return(std::move(value)));
    }
    if (peek().is("if")) {
        const std::size_t pos = peek().pos;
        at_++;
        expect("(");
        // **A declaration here is a scope that wraps both arms**, so the whole
        // statement goes inside a block of its own rather than the condition
        // being a bigger expression.
        const bool declares = atConditionDeclaration();
        if (!declares) warnAssignmentAsCondition();
        std::vector<StmtPtr> setup;
        int scope = -1;
        std::size_t aliveAtEntry = alive_.size();
        if (declares) {
            enterScope();
            scope = enterBlock();
            aliveAtEntry = alive_.size();
        }
        ExprPtr cond = declares
            ? contextualScalar(ifConditionDeclaration(setup), peek().pos,
                               "this condition")
            : contextualScalar(endFullExpression(decay(expr())), peek().pos,
                               "this condition");
        // **Where the condition's object became alive**, in the shape a
        // cleanup region wants: a statement index and how many were alive
        // after it.
        std::vector<std::pair<std::size_t, std::size_t> > built;
        if (declares && alive_.size() != aliveAtEntry)
            built.push_back(std::make_pair(setup.size(), alive_.size()));
        expect(")");
        StmtPtr thenArm = statement();
        StmtPtr elseArm;
        if (consume("else")) elseArm = statement();
        StmtPtr made(new If(std::move(cond), std::move(thenArm),
                            std::move(elseArm)));
        if (!declares) return made;
        leaveBlock();
        leaveScope();
        setup.push_back(std::move(made));
        // What the condition built is destroyed at the end of the statement,
        // the way a block destroys what it built at its '}'.
        if (alive_.size() != aliveAtEntry) {
            emitDestructors(setup, aliveAtEntry, pos);
            setup = target_.microsoftNames()
                        ? wrapMsCleanups(std::move(setup), built,
                                         aliveAtEntry, pos)
                        : wrapCleanups(std::move(setup), built,
                                       aliveAtEntry, pos);
            alive_.resize(aliveAtEntry);
        }
        Block *b = new Block(std::move(setup));
        b->setScope(scope);
        b->setPos(pos);
        return StmtPtr(b);
    }
    if (peek().is("while")) {
        const std::size_t pos = peek().pos;
        at_++;
        expect("(");
        // The declared object is one slot for the whole loop and is written
        // afresh each turn - see whileConditionDeclaration. It still needs a
        // scope of its own, or the name would outlive the loop.
        const bool declares = atConditionDeclaration();
        if (!declares) warnAssignmentAsCondition();
        if (declares && conditionDeclaresObject()) {
            enterScope();
            const int outer = enterBlock();
            StmtPtr loop = declaringLoop(pos, false);
            leaveBlock();
            leaveScope();
            std::vector<StmtPtr> wrap;
            wrap.push_back(std::move(loop));
            Block *b = new Block(std::move(wrap));
            b->setScope(outer);
            b->setPos(pos);
            return StmtPtr(b);
        }
        int scope = -1;
        if (declares) { enterScope(); scope = enterBlock(); }
        ExprPtr cond = declares
            ? contextualScalar(endFullExpression(whileConditionDeclaration()),
                               peek().pos, "this condition")
            : contextualScalar(endFullExpression(decay(expr())), peek().pos,
                               "this condition");
        expect(")");
        loopDepth_++;
        loopMarks_.push_back(alive_.size());
        breakMarks_.push_back(alive_.size());
        StmtPtr body = statement();
        breakMarks_.pop_back();
        loopMarks_.pop_back();
        loopDepth_--;
        StmtPtr made(new While(std::move(cond), std::move(body)));
        if (!declares) return made;
        leaveBlock();
        leaveScope();
        std::vector<StmtPtr> wrap;
        wrap.push_back(std::move(made));
        Block *b = new Block(std::move(wrap));
        b->setScope(scope);
        b->setPos(pos);
        return StmtPtr(b);
    }

    if (peek().is("for")) return forStatement();

    if (consume("do")) {
        loopDepth_++;
        loopMarks_.push_back(alive_.size());
        breakMarks_.push_back(alive_.size());
        StmtPtr body = statement();
        breakMarks_.pop_back();
        loopMarks_.pop_back();
        loopDepth_--;
        expect("while");
        expect("(");
        ExprPtr cond = contextualScalar(endFullExpression(decay(expr())), peek().pos,
                                   "this condition");
        expect(")");
        expect(";");
        return StmtPtr(new DoWhile(std::move(body), std::move(cond)));
    }

    if (peek().is("switch")) return switchStatement();
    if (peek().is("case") || peek().is("default")) return caseLabel();

    if (consume("goto")) {
        std::size_t pos = peek().pos;
        std::string name = expectIdent("a label to jump to");
        expect(";");
        return gotoStatement(name, pos, true);
    }

    if (peek().kind == TokenKind::Ident && peekAt(1).is(":")) return gotoLabel();

    // A jump can leave a scope without falling off its end, and this compiler runs
    // destructors at the end - so each of these destroys what was built since its loop
    // or switch was entered, the way `return` destroys what the function owes.
    if (peek().is("break")) {
        const std::size_t pos = peek().pos;
        at_++;
        if (loopDepth_ == 0 && switchDepth_ == 0)
            src_.fail(pos, "'break' is not inside a loop or a switch");
        expect(";");
        // **Out of a Microsoft handler it leaves the funclet**, and the parent
        // breaks at the label - a `Break` there, inside the loop the try is in.
        if (target_.microsoftNames() && handlersLeftByBreak() > 0 && msExit_ != nullptr)
            return jumpLeaving(StmtPtr(new FuncletLeave(msExitLabel(msExit_->brk, "brk"))),
                               breakMarks_.back(), pos, handlersLeftByBreak());
        return jumpLeaving(StmtPtr(new Break()), breakMarks_.back(), pos,
                           handlersLeftByBreak());
    }

    if (peek().is("continue")) {
        const std::size_t pos = peek().pos;
        at_++;
        if (loopDepth_ == 0)
            src_.fail(pos, "'continue' is not inside a loop");
        expect(";");
        if (target_.microsoftNames() && handlersLeftByContinue() > 0 && msExit_ != nullptr)
            return jumpLeaving(StmtPtr(new FuncletLeave(msExitLabel(msExit_->cont, "cont"))),
                               loopMarks_.back(), pos, handlersLeftByContinue());
        return jumpLeaving(StmtPtr(new Continue()), loopMarks_.back(), pos,
                           handlersLeftByContinue());
    }
    if (peek().is("{")) return block();
    if (consume(";")) return declarativeStatement();

    ExprPtr e = endFullExpression(expr());
    expect(";");
    return StmtPtr(new ExprStmt(std::move(e)));
}

// extern "C" - [dcl.link]. Two forms: one declaration, or a brace-enclosed
// list of them. The list is not a scope: what it holds is declared where the
// specification is, and only the linkage of the names changes.

// **One number per type, however many call-site rows name it.**
int Parser::typeIndexFor(const std::string &symbol) {
    for (std::size_t i = 0; i < functionTypes_.size(); i++)
        if (functionTypes_[i] == symbol) return static_cast<int>(i) + 1;
    functionTypes_.push_back(symbol);
    return static_cast<int>(functionTypes_.size());
}
