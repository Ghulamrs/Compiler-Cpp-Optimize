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
Parser::Init Parser::parenthesisedInitialiser(const Declared &d) {
    expect("(");
    Init in;
    in.pos = peek().pos;
    in.value = assign();
    if (peek().is(","))
        src_.fail(peek().pos, "'" + d.name + "' has type '" +
                              d.type->describe() + "', which takes one "
                              "initialiser - a list in parentheses is for a "
                              "class with a constructor that takes them");
    expect(")");
    return in;
}

// **Is the `(` ahead an initialiser or a parameter list?**
bool Parser::atParenInitialiser() {
    const std::size_t save = at_;
    at_++;                                    // the '('
    // **A parameter list begins with a decl-specifier-seq**, and that is the whole question.
    const bool pack = peek().kind == TokenKind::Ident && peekAt(1).is("...");
    // `auto` among them: a parameter declared with it is C++14 and is refused
    // by name where the parameter list is read, which only happens if this
    // says parameter list.
    const bool parameters = peek().is(")") || peek().is("...") || pack ||
                            peek().is("static") || peek().is("register") ||
                            peek().is("auto") || atTypeName();
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
    StmtPtr s = declarationBody();
    if (s) s->setPos(pos);
    return s;
}

StmtPtr Parser::declarationBody() {
    StorageClass sc;
    Qualifiers quals;
    const Type *base = specifiers(&sc, &quals);

    if (peek().is(";")) { at_++; return StmtPtr(new Block({})); }

    if (sc == StorageTypedef) {
        do {
            Declared td = declarator(base);
            typedefFunctionSuffix(td);
            // [dcl.typedef]/2 lets a typedef-name be redeclared to the same type,
            // which is what makes the C idiom "typedef struct S S;" legal now that
            // the tag names the type by itself. Only a different type is an error.
            if (const Type *had = findTypedef(td.name))
                if (had != td.type)
                    src_.fail(td.pos, "'" + td.name + "' is typedefed twice, "
                                      "and not to the same type: it was '" +
                                      had->describe() + "' and is now '" +
                                      td.type->describe() + "'");
            typedefIndex_[td.name] = typedefs_.size();
            typedefs_.push_back(TypedefName{ td.name, td.type });
        } while (consume(","));
        expect(";");
        return StmtPtr(new Block({}));
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
        if (mentionsDeduced(d.type)) {
            d.type = deduceAuto(d.type, d.name, d.pos);
            checkOneDeducedType(deducedSoFar, lastDeducedAuto_, d.name, d.pos);
        }

        // **A const object has to be initialised where it is declared**, and
        // this is asked before the branch below rather than after it.
        if (d.type->isConst() && !peek().is("=") && !peek().is("(") &&
            !peek().is("{"))
            requireConstInitialised(d.type, d.name, d.pos);

        // An object of a class with constructors is built by calling one, asked before
        // the branch below - `Point p(1)` and a function declaration look alike until
        // the type is known. **And an array of one**, which used to fall through.
        {
            const Type *elem = d.type;
            while (elem != nullptr && elem->isArray()) elem = elem->pointee();
            const Type *plain = elem == nullptr ? nullptr : elem->unqualified();
            if (d.type->isArray() && plain != nullptr &&
                plain->isStructOrUnion() && !plain->tag().empty() &&
                overloadsOf(constructorKey(plain->tag())) != nullptr) {
                if (sc == StorageStatic)
                    src_.fail(d.pos, "'" + d.name + "' is static and its "
                                     "elements have a constructor - an array "
                                     "with static storage duration whose "
                                     "elements have a constructor is not "
                                     "supported yet");
                if (peek().is("(") || peek().is("="))
                    src_.fail(d.pos, "an initialiser for an array of '" +
                                     plain->describe() + "' is not supported "
                                     "yet - each element gets the default "
                                     "constructor");
                // **Destruction is refused rather than half-built.** [class.dtor]
                // destroys the elements in reverse, and the shared code that emits a
                // scope's destructors knows one object per entry and not a count.
                if (destructorOf(plain) != nullptr)
                    src_.fail(d.pos, "an array of '" + plain->describe() +
                                     "' is not supported yet because it has a "
                                     "destructor, and the elements would have "
                                     "to be destroyed in reverse when the scope "
                                     "ends - an array of a class with only "
                                     "constructors works");
                int off = declare(d.name, d.type, d.pos);
                locals_.back().guardsJump = true;
                int indexSlot = allocateFrameSlot(types_.intType());
                inits.push_back(constructLocalArray(d, off, indexSlot));
                // **The comma belongs to the loop condition.**
                continue;
            }
        }

        if (d.type->isStructOrUnion() && !d.type->tag().empty() &&
            overloadsOf(constructorKey(d.type->tag())) != nullptr) {
            // [stmt.dcl]/4: a static one is built the first time control
            // passes through, under a guard, and destroyed at exit.
            if (sc == StorageStatic) {
                staticLocalWithConstructor(d, inits);
                continue;
            }
            CtorInit ci = readConstructorInitialiser(d);

            int off = declare(d.name, d.type, d.pos);
            locals_.back().guardsJump = true;

            // The backing array and the list object, before the constructor
            // that reads them - a list-init only.
            for (std::size_t z = 0; z < ci.ilSetup.size(); z++)
                inits.push_back(std::move(ci.ilSetup[z]));

            // **Copy elision, in the one case worth having it**: where the initialiser is a call
            // already returning through a hidden pointer, the object is built straight into this
            // variable. clang does it at -O0, cl does not; both may.
            Call *made = ci.args.size() == 1 &&
                         (d.type->nonTrivialCopy() ||
                          destructorOf(d.type) != nullptr)
                       ? dynamic_cast<Call *>(ci.args[0].get()) : nullptr;

            if (made != nullptr && made->type() == d.type &&
                returnsIndirectly(d.type, made->hasThis())) {
                claimCallResult(*made, off);
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
            flushTemporaries(inits);
            if (destructorOf(d.type) != nullptr)
                alive_.push_back(Alive{ d.name, off, d.type->unqualified() });
            // **The comma belongs to the loop condition.**
            continue;
        }

        // **`X q(p);` where X has no constructor at all.** Its copy is trivial, so what
        // the standard asks for is the bytes - the struct assignment the backends
        // already emit. A parameter list begins with a type name and this does not.
        if (peek().is("(") && d.type->isStructOrUnion() && sc != StorageStatic) {
            if (atParenInitialiser()) {
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
                const int off = declare(d.name, d.type, d.pos);
                locals_.back().guardsJump = true;
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
        // **Not in a condition**, which takes `= expr` or braces and nothing
        // else - [stmt.select]/1 spells the grammar out, and clang says so by
        // name.
        if (parenInit && conditionDecl_)
            src_.fail(peek().pos, "a declaration in a condition is initialised "
                                  "with '=' or with braces, not with "
                                  "parentheses - [stmt.select] gives the "
                                  "condition its own grammar, and this one "
                                  "would read like a call");

        if (peek().is("(") && !parenInit) {
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
                bindStaticReference(d, symbol, pieces, hasInit, &body);
                declareStaticLocal(d.name, d.type, d.pos, symbol);
                current_->globals.push_back(Global{ symbol, symbol,
                                                    types_.pointerTo(d.type->referent()),
                                                    std::move(pieces), hasInit,
                                                    true, false });
                if (!body.empty())
                    inits.push_back(guardOnce(symbol, std::move(body)));
                continue;
            }
            if (!peek().is("="))
                src_.fail(d.pos, "'" + d.name + "' is a reference and has to be "
                                 "initialised here - there is no later "
                                 "assignment that would bind it, only one that "
                                 "writes through it");
            at_++;
            ExprPtr init = assign();
            int off = declare(d.name, d.type, d.pos);
            locals_.back().guardsJump = true;
            const Type *slot = types_.pointerTo(d.type->referent());
            ExprPtr addr = bindReference(d.type, std::move(init), d.pos,
                                         "'" + d.name + "'");
            ExprPtr target(Var::local(d.name, off));
            target->setType(slot);
            // **The temporary this reference binds to, if it made one**, is
            // extended to the scope rather than destroyed at the semicolon -
            // [class.temporary]/5.
            const bool extended = extendTemporary(*addr, d.name + "$held");
            ExprPtr bind(new Assign(std::move(target), std::move(addr)));
            bind->setType(slot);
            inits.push_back(StmtPtr(new ExprStmt(std::move(bind))));
            // And anything else the initialiser built - `const T &r =
            // f(T(1));` makes T(1) as well, and that one does die here. **Only
            // once something was extended.**
            if (extended) flushTemporaries(inits);
            continue;
        }

        bool sizedByInitialiser = d.type->isArray() && d.type->length() < 0 &&
                                  peek().is("=");
        if (!d.type->isComplete() && !sizedByInitialiser)
            src_.fail(d.pos, "'" + d.name + "' has an incomplete type");
        checkNotAbstract(d.type, d.pos, "'" + d.name + "'");

        if (sc == StorageStatic) {
            const std::string symbol = uniqueStaticSymbol(d.name);
            std::vector<GlobalPiece> pieces;
            bool hasInit = false;
            if (parenInit || consume("=") || atBracedInitialiser(d.name)) {
                Init in = parenInit ? parenthesisedInitialiser(d)
                                    : parseInitialiser();
                if (d.type->isArray() && d.type->length() < 0)
                    d.type = types_.arrayOf(d.type->pointee(),
                                            inferredLength(in, d.type->pointee(), d.pos));
                flattenInit(d.type, in, 0, pieces);
                hasInit = true;
            } else if (d.type->isArray() && d.type->length() < 0) {
                src_.fail(d.pos, "'" + d.name + "' has no length and no initialiser "
                                 "to take one from");
            }
            declareStaticLocal(d.name, d.type, d.pos, symbol);
            locals_.back().isConst = d.type->isConst();
            current_->globals.push_back(Global{ symbol, symbol, d.type,
                                                std::move(pieces), hasInit, true,
                                                locals_.back().isConst });
            continue;
        }

        bool hasInit = parenInit || peek().is("=") || atBracedInitialiser(d.name);
        Init in;
        if (hasInit) {
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
        const int off = declare(d.name, d.type, d.pos);
        locals_.back().isConst = d.type->isConst();
        locals_.back().isRegister = (sc == StorageRegister);
        // An initialiser to skip, or a destructor that would run on what was
        // never built: either makes this a declaration no jump may land past.
        locals_.back().guardsJump = hasInit || destructorOf(d.type) != nullptr;
        if (hasInit) {
            long long value = 0;
            long double dvalue = 0;
            if (constantInitialiser(d.type, in, &value)) {
                locals_.back().isConstantValue = true;
                locals_.back().constantValue = value;
            } else if (constantFloatingInitialiser(d.type, in, &dvalue)) {
                // A const floating local reads back the same way a global does.
                locals_.back().isConstantDouble = true;
                locals_.back().constantDouble = dvalue;
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
            emitInit(d.name, path, d.type, in, inits);
        }
        flushTemporaries(inits);
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

    // **A temporary range needs its lifetime extended and this does not do
    // that.**
    if (!isGlvalue(*range))
        src_.fail(rpos, "the range here is a temporary, and a range-based "
                        "'for' binds the range to a reference that keeps it "
                        "alive for the whole loop - which is not supported "
                        "yet. Name it in a variable first and loop over that");

    if (findMemberOwner(cls, "begin") == nullptr ||
        findMemberOwner(cls, "end") == nullptr)
        src_.fail(rpos, "'" + cls->describe() + "' has no 'begin' and 'end' "
                        "member function, and a free 'begin(r)' and 'end(r)' "
                        "found by argument-dependent lookup - which is what a "
                        "range-based 'for' falls back to - is not supported "
                        "yet");

    // `R *__r = &range;`
    const Type *rangePtr = types_.pointerTo(rangeType);
    *bSlot = declare(".rr" + std::to_string(refTemps_), rangePtr, rpos);
    const std::string rName = ".rr" + std::to_string(refTemps_++);
    const int rSlot = *bSlot;
    ExprPtr held(Var::local(rName, rSlot));
    held->setType(rangePtr);
    ExprPtr addr(new Unary('&', std::move(range)));
    addr->setType(rangePtr);
    ExprPtr keep(new Assign(std::move(held), std::move(addr)));
    keep->setType(rangePtr);
    setup.push_back(StmtPtr(new ExprStmt(std::move(keep))));

    // `*__r`, rebuilt for each call: an ExprPtr is used up by the one that
    // takes it, and `memberCallWith` takes the object's address itself.
    auto object = [&]() {
        ExprPtr p(Var::local(rName, rSlot));
        p->setType(rangePtr);
        ExprPtr o(new Unary('*', std::move(p)));
        o->setType(rangeType);
        return o;
    };

    ExprPtr first = memberCallWith(object(), rangeType, "begin", rpos,
                                   std::vector<ExprPtr>());
    const Type *iter = first->type();

    // **The iterator has to be a pointer, and every one in `include/` is.**
    if (!iter->isPointer())
        src_.fail(rpos, "'" + cls->describe() + "::begin()' returns '" +
                        iter->describe() + "', and a range-based 'for' over a "
                        "class whose iterator is not a pointer is not "
                        "supported yet - the loop would have to call its "
                        "'operator!=', 'operator++' and 'operator*'");

    *bSlot = declare(".rb" + std::to_string(refTemps_), iter, rpos);
    *bName = ".rb" + std::to_string(refTemps_++);
    ExprPtr b(Var::local(*bName, *bSlot));
    b->setType(iter);
    ExprPtr startAt(new Assign(std::move(b), std::move(first)));
    startAt->setType(iter);
    setup.push_back(StmtPtr(new ExprStmt(std::move(startAt))));

    ExprPtr last = memberCallWith(object(), rangeType, "end", rpos,
                                  std::vector<ExprPtr>());
    if (last->type() != iter)
        src_.fail(rpos, "'" + cls->describe() + "::begin()' and 'end()' return "
                        "different types, '" + iter->describe() + "' and '" +
                        last->type()->describe() + "', so there is nothing the "
                        "loop can compare");

    *eSlot = declare(".re" + std::to_string(refTemps_), iter, rpos);
    *eName = ".re" + std::to_string(refTemps_++);
    ExprPtr e(Var::local(*eName, *eSlot));
    e->setType(iter);
    ExprPtr stopAt(new Assign(std::move(e), std::move(last)));
    stopAt->setType(iter);
    setup.push_back(StmtPtr(new ExprStmt(std::move(stopAt))));

    return iter;
}

StmtPtr Parser::rangeForStatement(int scope) {
    StorageClass sc;
    Qualifiers quals;
    const Type *base = specifiers(&sc, &quals);
    Declared d = declarator(base);
    expect(":");

    const std::size_t rpos = peek().pos;
    // **A braced list as the range is an `initializer_list`** - [stmt.ranged]
    // binds the range to `auto &&`, and for braces that makes one of those.
    if (peek().is("{"))
        src_.fail(rpos, "a range-based 'for' over a braced list is not "
                        "supported yet - the list would be an "
                        "'std::initializer_list', which this compiler has no "
                        "library for; name an array and loop over that");
    ExprPtr range = expr();
    expect(")");

    const Type *rt = range->type();
    if (d.type->isReference())
        src_.fail(d.pos, "a reference in a range-based 'for' is not supported "
                         "yet - the loop variable is copied for now");

    // **The two ends of the loop, and the only thing the two kinds of range
    // disagree about.**
    const Type *elemPtr = nullptr;
    std::vector<StmtPtr> setup;
    int bSlot = 0, eSlot = 0;
    std::string bName, eName;

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

        // `T *__b = a;` - the array decayed, evaluated here and nowhere else.
        bSlot = declare(".rb" + std::to_string(refTemps_), elemPtr, rpos);
        bName = ".rb" + std::to_string(refTemps_++);
        ExprPtr b(Var::local(bName, bSlot));
        b->setType(elemPtr);
        ExprPtr startAt(new Assign(std::move(b), decay(std::move(range))));
        startAt->setType(elemPtr);
        setup.push_back(StmtPtr(new ExprStmt(std::move(startAt))));

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

    const Type *elem = elemPtr->pointee();
    if (mentionsDeduced(d.type))
        d.type = deduceAutoFrom(d.type, elem, d.name, d.pos);

    // `__b != __e`
    ExprPtr atB(Var::local(bName, bSlot));
    atB->setType(elemPtr);
    ExprPtr atE(Var::local(eName, eSlot));
    atE->setType(elemPtr);
    ExprPtr cond = comparison(BinOp::Ne, std::move(atB), std::move(atE), rpos);

    // `__b = __b + 1`
    ExprPtr stepFrom(Var::local(bName, bSlot));
    stepFrom->setType(elemPtr);
    ExprPtr one(new Num(1LL));
    one->setType(types_.get(target_.sizeType()));
    ExprPtr next = arithmetic(BinOp::Add, std::move(stepFrom), std::move(one),
                              rpos);
    ExprPtr stepTo(Var::local(bName, bSlot));
    stepTo->setType(elemPtr);
    ExprPtr step(new Assign(std::move(stepTo), std::move(next)));
    step->setType(elemPtr);

    // The body, with the loop variable built from `*__b` in front of it.
    enterScope();
    const int inner = enterBlock();
    const int vSlot = declare(d.name, d.type, d.pos);
    ExprPtr through(Var::local(bName, bSlot));
    through->setType(elemPtr);
    ExprPtr at(new Unary('*', std::move(through)));
    at->setType(elem);
    ExprPtr var(Var::local(d.name, vSlot));
    var->setType(d.type);
    ExprPtr take(new Assign(std::move(var), convert(std::move(at), d.type)));
    take->setType(d.type);

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
        d.type = deduceAutoFrom(d.type, init->type(), d.name, d.pos);
    // A class would have to be constructed and destroyed once per turn, and the construction is
    // written where the test is - so it is refused here and not in an `if`, where the object is
    // built once and the ordinary declaration path does all of it.
    if (d.type->isStructOrUnion() || d.type->isReference() || d.type->isArray())
        src_.fail(d.pos, "a '" + d.type->describe() + "' declared in the "
                         "condition of a loop is not supported yet - "
                         "[stmt.iter] builds it afresh on every turn, and only "
                         "a scalar can be written where the test is");
    const int slot = declare(d.name, d.type, d.pos);
    locals_.back().isConst = d.type->isConst();
    ExprPtr x(Var::local(d.name, slot));
    x->setType(d.type);
    ExprPtr set(new Assign(std::move(x), convert(std::move(init), d.type)));
    set->setType(d.type);
    return set;
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

    ExprPtr cond;
    if (!peek().is(";")) cond = endFullExpression(decay(expr()));
    expect(";");

    ExprPtr step;
    if (!peek().is(")")) step = endFullExpression(decay(expr()));
    expect(")");

    // The mark is taken after the init-statement: `for (S s; ...)` builds s
    // once for the whole loop, and a break must not destroy it.
    loopDepth_++;
    loopMarks_.push_back(alive_.size());
    breakMarks_.push_back(alive_.size());
    StmtPtr body = statement();
    breakMarks_.pop_back();
    loopMarks_.pop_back();
    loopDepth_--;

    leaveBlock();
    leaveScope();
    For *f = new For(std::move(init), std::move(cond),
                     std::move(step), std::move(body));
    f->setScope(scope);
    if (alive_.size() == aliveAtEntry) return StmtPtr(f);

    // **`for (S s; ...)` destroys s when the loop is done**, here, the way a block
    // destroys what it built at its '}'. It used to stay on alive_ and be destroyed by
    // the enclosing block instead - once, but late, and after all that block did.
    if (functionHasTry_ || inTryBody_)
        src_.fail(pos, "a local with a destructor and a 'try' in one "
                       "function is not supported yet - each is a range in "
                       "the call-site table and one would have to split "
                       "the other");
    std::vector<StmtPtr> steps;
    steps.push_back(StmtPtr(f));
    emitDestructors(steps, aliveAtEntry, pos);
    alive_.resize(aliveAtEntry);
    Block *b = new Block(std::move(steps));
    b->setScope(-1);
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
        value = narrowTo(constantExpression("a case value"),
                         switches_.back().governing);
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

    if (atDeclarationStart())
        src_.fail(peek().pos, "a label cannot be followed by a declaration - "
                              "put it in a block");
    if (peek().is("}"))
        src_.fail(peek().pos, "a label must be followed by a statement");

    StmtPtr body = statement();

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
    labels_.push_back(LabelDef{ name, pos, jumpGuards(), alive_, nullptr });

    if (atDeclarationStart())
        src_.fail(peek().pos, "a label cannot be followed by a declaration - "
                              "put it in a block");
    if (peek().is("}"))
        src_.fail(peek().pos, "a label must be followed by a statement");

    return StmtPtr(new Label(std::move(name), statement()));
}

// **[stmt.dcl]/3: a jump may not enter the scope of an initialised object.** All three
// jumps this compiler has fall under it, and clang refuses each. The test is set
// membership: refused when the label's live list holds one the origin's does not.
std::vector<Parser::JumpGuard> Parser::jumpGuards() const {
    std::vector<JumpGuard> out;
    for (const Local &l : locals_)
        if (l.guardsJump) out.push_back(JumpGuard{ l.name, l.offset });
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

void Parser::checkJump(const std::vector<JumpGuard> &from,
                       const std::vector<JumpGuard> &to, std::size_t pos,
                       const std::string &jump, const std::string &origin) const {
    for (const JumpGuard &g : to) {
        bool inScopeAtOrigin = false;
        for (const JumpGuard &f : from)
            if (f.offset == g.offset) { inScopeAtOrigin = true; break; }
        if (inScopeAtOrigin) continue;
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
        for (std::size_t i = g.alive.size(); i > 0; i--) {
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
    }
    labels_.clear();
    gotos_.clear();
}

StmtPtr Parser::block() {
    std::size_t pos = peek().pos;
    expect("{");
    enterScope();
    // A `using namespace` written inside this block reaches the '}' and no
    // further - [namespace.udir]/2 - so the list is cut back to what it held
    // on the way in.
    const std::size_t usingAtEntry = usingNamespaces_.size();
    const std::size_t aliveAtEntry = alive_.size();
    bool isBody = atFunctionBody_;
    atFunctionBody_ = false;
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
    while (!peek().is("}")) {
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
        if (alive_.size() > aliveBefore)
            built.push_back(std::make_pair(body.size(), alive_.size()));
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
        const bool overlapping = target_.microsoftNames()
                                     ? (functionHasTry_ || inTryBody_)
                                     : false;
        if (overlapping)
            src_.fail(pos, target_.microsoftNames()
                ? "a local with a destructor and a 'try' in one function is "
                  "not supported yet for x86_64-windows - a cleanup there is "
                  "a funclet and a state in the FH3 tables, and only the "
                  "Itanium targets have been taught to split one around the "
                  "other"
                : "a local with a destructor inside a 'catch' handler is not "
                  "supported yet - a handler is emitted past the 'try''s range, "
                  "so its cleanup region is inside no row and has no chain to "
                  "hand the selector to; inside the 'try' body works, and so "
                  "does beside the 'try' in the same block");
        body = target_.microsoftNames()
                   ? wrapMsCleanups(std::move(body), built, regionFrom, pos,
                                    temps)
                   : wrapCleanups(std::move(body), built, regionFrom, pos,
                                  temps, tryAt);
    }
    alive_.resize(aliveAtEntry);
    usingNamespaces_.resize(usingAtEntry);

    expect("}");
    if (!isBody) leaveBlock();
    leaveScope();
    Block *b = new Block(std::move(body));
    b->setScope(scope);

    b->setPos(pos);
    return StmtPtr(b);
}

StmtPtr Parser::statement() {
    std::size_t pos = peek().pos;
    StmtPtr s = statementBody();
    if (s) s->setPos(pos);
    return s;
}

// **`try` is a block, a landing pad, and no new statement machinery.**
StmtPtr Parser::tryStatement(std::size_t pos) {
    // What is alive when the `try` is reached.
    const std::size_t aliveOutside = alive_.size();
    // **The two ABIs disagree about who picks the handler**, so this reads one grammar
    // and builds two shapes: Itanium's if/else chain on a selector, and Microsoft's
    // handlers kept whole for the runtime to call.
    const bool microsoft = target_.microsoftNames();
    functionHasTry_ = true;
    // **A handler's body is inside the same table and was not refused**, which
    // made this compile and terminate rather than say so.
    if ((inTryBody_ || inHandlerBody_) && target_.microsoftNames())
        src_.fail(pos, "a 'try' inside another one is not supported yet for "
                       "x86_64-windows - a handler there is a funclet named "
                       "after its function and a counter, and a nested one "
                       "takes a name already used, which ml64 answers with "
                       "'A2005: symbol redefinition'. It works on both Itanium "
                       "targets");

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
    inTryBody_ = true;
    if (!peek().is("{"))
        src_.fail(peek().pos, "'try' takes a block");
    StmtPtr body = block();
    inTryBody_ = wasInTry;
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

    if (!peek().is("catch"))
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
    std::vector<int> indices;
    std::vector<std::string> types;
    bool sawCatchAll = false;

    while (peek().is("catch")) {
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
            declaredType = byRef ? d.type : d.type->unqualified();
            caught = byRef ? d.type->referent()->unqualified()
                           : d.type->unqualified();
            std::string why;
            h.type = typeInfoSymbolFor(caught, cpos, &why);
            if (h.type.empty())
                src_.fail(cpos, "'catch' cannot name this type: " + why);
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
            if (caught != nullptr) {
                MicrosoftThrow names;
                std::string why;
                if (!microsoftThrowNames(caught, caught->size(target_),
                                         &names, &why))
                    src_.fail(cpos, "'catch' cannot name this type: " + why);
                mh.descriptor = names.descriptor;
                mh.objectSize = caught->size(target_);
                mh.byReference = byRef;
                // The descriptor is emitted by the same pass that emits a thrown
                // type's, so a type that is only ever *caught* has to join that list
                // or the handler map would name a symbol nothing defines.
                bool had = false;
                for (std::size_t k = 0; k < current_->thrown.size(); k++)
                    if (current_->thrown[k] == caught) had = true;
                if (!had) current_->thrown.push_back(caught);
                if (!caughtName.empty())
                    mh.objectSlot = declare(caughtName, declaredType, cpos);
            }
            if (!peek().is("{")) src_.fail(peek().pos, "'catch' takes a block");
            const bool wasInHandler = inMsHandler_;
            const bool wasBody = inHandlerBody_;
            inMsHandler_ = true;
            inHandlerBody_ = true;
            // **The same bookkeeping, for the opposite purpose.**
            handlerDepth_++;
            handlerLoopDepth_.push_back(loopDepth_);
            handlerSwitchDepth_.push_back(switchDepth_);
            handlerFrom_.push_back(peek().pos);
            mh.body = block();
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
            // **A reference holds what __cxa_begin_catch handed back.**
            const Type *slotType = byRef
                ? types_.pointerTo(declaredType->referent())
                : types_.pointerTo(caught);
            ExprPtr cast;
            if (byRef) {
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
        if (!microsoft) {
            handlerResumeLabel_ = endCatchLabel;
            handlerResumePtr_ = padPtr;
            handlerResumeSel_ = padSel;
        }
        StmtPtr handlerBody = block();
        handlerResumeLabel_ = wasHandlerLabel2;
        handlerResumePtr_ = wasHandlerPtr2;
        handlerResumeSel_ = wasHandlerSel2;
        const bool canThrow = mayThrow_ > throwsBefore;
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
            if (!beyond.empty()) {
                // **Handed on rather than resumed.**
                padSteps.push_back(StmtPtr(new Goto(beyond)));
            } else {
                std::vector<ExprPtr> resumeArgs;
                ExprPtr held(Var::local(".ex.ptr", padPtr));
                held->setType(voidPtr);
                resumeArgs.push_back(std::move(held));
                padSteps.push_back(StmtPtr(new ExprStmt(
                    resumeCall(
                                std::move(resumeArgs)))));
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
        t->setUnwindHelpSlot(allocateFrameSlot(voidPtr));
        return StmtPtr(t);
    }

    // **Nothing matched, so this frame unwinds like any other.**
    std::vector<ExprPtr> resumeArgs;
    ExprPtr again(Var::local(".ex.ptr", pointerSlot));
    again->setType(voidPtr);
    resumeArgs.push_back(std::move(again));
    std::vector<StmtPtr> resume;
    // **A nested `try` hands over instead of resuming.**
    int outPtr = 0, outSel = 0;
    const std::string beyondTry = unwindTarget(&outPtr, &outSel);
    const bool unwindsHere = aliveOutside > bodyCleanupFrom_ &&
                             beyondTry.empty();
    if (unwindsHere) emitDestructors(resume, bodyCleanupFrom_, pos,
                                     -1, aliveOutside);
    if (beyondTry.empty())
        resume.push_back(StmtPtr(new ExprStmt(
            resumeCall(
                        std::move(resumeArgs)))));
    else
        resume.push_back(StmtPtr(new Goto(beyondTry)));
    StmtPtr chain = unwindPad(std::move(resume));

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

StmtPtr Parser::statementBody() {
    // A static_assert declares nothing, so the statement it becomes is empty.
    if (staticAssertion()) return StmtPtr(new Block({}));

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
        if (namespaces_.find(opened) == namespaces_.end())
            src_.fail(peek().pos, "'" + opened + "' is not a namespace");
        usingNamespaces_.push_back(opened);
        return StmtPtr(new Block({}));
    }

    // **The using-*declaration* is refused inside a block**, where the one at namespace scope is
    // not: a name declared here lasts to the end of the block and takes part in overload resolution
    // against the locals beside it, and neither is what the alias at namespace scope does.
    refuseAliasDeclaration();
    if (peek().is("using"))
        src_.fail(peek().pos, "a using-declaration inside a block is not "
                              "supported yet - it declares a name for the rest "
                              "of this block rather than naming one; at "
                              "namespace scope it works, and 'using namespace "
                              "N;' works here");

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
            if (target_.microsoftNames())
                src_.fail(tpos, "a rethrow - 'throw' with nothing after it - "
                                "is not supported yet for x86_64-windows: it "
                                "is _CxxThrowException with two null pointers "
                                "from inside a funclet, and that has not been "
                                "measured on the box");
            // [except.throw]/8: with no exception being handled this calls
            // std::terminate, which is what __cxa_rethrow does by itself -
            // so it is well-formed here, as clang has it, and needs no check.
            mayThrow_++;
            return StmtPtr(new ExprStmt(
                runtimeCall("__cxa_rethrow", types_.get(Kind::Void),
                            std::vector<ExprPtr>())));
        }
        mayThrow_++;
        ExprPtr value = decay(expr());
        // **A thrown expression's temporary is destroyed by the throw itself.**
        expect(";");
        // **[except.spec]/9: an exception that escapes a `noexcept` function
        // calls std::terminate.**
        if (inNoexceptFunction_ && !inTryBody_ && !inHandlerBody_) {
            std::vector<StmtPtr> both;
            both.push_back(StmtPtr(new ExprStmt(std::move(value))));
            both.push_back(StmtPtr(new ExprStmt(
                runtimeCall("abort", types_.get(Kind::Void),
                            std::vector<ExprPtr>()))));
            return StmtPtr(new Block(std::move(both)));
        }
        return throwStatement(std::move(value), tpos);
    }

    if (peek().is("return") && inMsHandler_)
        src_.fail(peek().pos, "'return' inside a 'catch' is not supported yet "
                              "for x86_64-windows - a handler is compiled as a "
                              "function of its own there, and leaving it means "
                              "handing back the address to carry on at in the "
                              "register a return value would travel in");
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
                unwind.push_back(StmtPtr(new Return(nullptr)));
                return StmtPtr(new Block(std::move(unwind)));
            }
            return StmtPtr(new Return(nullptr));
        }
        ExprPtr returned = returnType_->isReference() ? expr() : decay(expr());
        // **The temporary that *is* the returned value must not be destroyed
        // here.**
        if (!returnType_->isReference())
            if (ExprPtr made = userConversion(returnType_, returned, pos))
                returned = std::move(made);
        bool ownedTemporary = false;
        if (!returnType_->isReference() && returnType_->isStructOrUnion())
            ownedTemporary = releaseTemporary(*returned);
        ExprPtr value = endFullExpression(std::move(returned));
        bool returnedParameter = false;
        if (returnType_->isReference()) {
            value = bindReference(returnType_, std::move(value), pos,
                                  "this function's return type");
            if (dynamic_cast<const Comma *>(value.get()) != nullptr)
                src_.fail(pos, "this returns a reference to a temporary of "
                               "this function, which is gone by the time the "
                               "caller could read it");
        } else {
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
                unwind.push_back(StmtPtr(new Return(nullptr)));
                return StmtPtr(new Block(std::move(unwind)));
            }
            // **The value is computed into a slot before the catch ends**, and
            // that is not only tidiness: `return e.v;` reads the caught object,
            // which `__cxa_end_catch` destroys.
            int slot = allocateFrameSlot(returnType_);
            std::string temp = ".ret" + std::to_string(refTemps_++);

            ExprPtr keep(Var::local(temp, slot));
            keep->setType(returnType_);
            ExprPtr save(new Assign(std::move(keep), std::move(value)));
            save->setType(returnType_);
            unwind.push_back(StmtPtr(new ExprStmt(std::move(save))));

            emitDestructors(unwind, 0, pos, elided);
            endCatches(unwind, handlerDepth_);

            ExprPtr give(Var::local(temp, slot));
            give->setType(returnType_);
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
        const bool declares = atDeclarationStart();
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
            if (functionHasTry_ || inTryBody_)
                src_.fail(pos, "a local with a destructor and a 'try' in one "
                               "function is not supported yet - each is a "
                               "range in the call-site table and one would "
                               "have to split the other");
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
        const bool declares = atDeclarationStart();
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
        // **A `goto` out of a handler ends the handling** -
        // [except.handle]/16, the same sentence `return`, `break` and
        // `continue` answer above - and this one cannot.
        if (handlerDepth_ > 0) {
            bool insideHandler = false;
            for (std::size_t i = 0; i < labels_.size(); i++)
                if (labels_[i].name == name && labels_[i].pos >= handlerFrom_.back())
                    insideHandler = true;
            if (!insideHandler)
                src_.fail(pos, "'goto " + name + "' leaves a 'catch' block, "
                               "which is not supported yet - leaving a handler "
                               "ends the handling, and the call that ends it "
                               "is emitted where the jump is written, which a "
                               "label this compiler has not read yet cannot "
                               "have. A 'return' out of a handler works, and "
                               "so does a 'goto' to a label inside it");
        }
        // **The jump destroys what it leaves, and cannot yet know what that is**: a
        // forward label has not been read. So the goto is placed behind an empty block
        // that resolveGotos() fills. What is alive here is copied now, not later.
        Block *cleanups = new Block({});
        cleanups->setScope(-1);
        gotos_.push_back(LabelDef{ name, pos, jumpGuards(), alive_, cleanups });
        std::vector<StmtPtr> steps;
        steps.push_back(StmtPtr(cleanups));
        steps.push_back(StmtPtr(new Goto(std::move(name))));
        Block *b = new Block(std::move(steps));
        b->setScope(-1);
        return StmtPtr(b);
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
        if (target_.microsoftNames() && handlersLeftByBreak() > 0)
            src_.fail(pos, "'break' out of a 'catch' block is not supported "
                           "yet for x86_64-windows - a handler is compiled as "
                           "a function of its own there, and leaving it early "
                           "means handing back the address to carry on at, "
                           "which is what 'return' is refused for here too");
        return jumpLeaving(StmtPtr(new Break()), breakMarks_.back(), pos,
                           handlersLeftByBreak());
    }

    if (peek().is("continue")) {
        const std::size_t pos = peek().pos;
        at_++;
        if (loopDepth_ == 0)
            src_.fail(pos, "'continue' is not inside a loop");
        expect(";");
        if (target_.microsoftNames() && handlersLeftByContinue() > 0)
            src_.fail(pos, "'continue' out of a 'catch' block is not supported "
                           "yet for x86_64-windows - a handler is compiled as "
                           "a function of its own there, and leaving it early "
                           "means handing back the address to carry on at, "
                           "which is what 'return' is refused for here too");
        return jumpLeaving(StmtPtr(new Continue()), loopMarks_.back(), pos,
                           handlersLeftByContinue());
    }
    if (peek().is("{")) return block();
    if (consume(";")) return StmtPtr(new Block({}));

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
