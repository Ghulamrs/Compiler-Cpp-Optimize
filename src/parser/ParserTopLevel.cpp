// The parser: the top level, and what a translation unit holds.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>

bool Parser::linkageSpecification() {
    if (!peek().is("extern") || peekAt(1).kind != TokenKind::Str) return false;
    at_++;
    std::size_t pos = peek().pos;
    std::string language = peek().text;
    at_++;

    if (language != "C" && language != "C++")
        src_.fail(pos, "'" + language + "' is not a linkage this compiler "
                       "knows - the standard fixes only \"C\" and \"C++\", "
                       "and every other spelling is the implementation's own");

    bool c = language == "C";
    if (c) cLinkage_++;

    if (consume("{")) {
        while (!peek().is("}")) {
            if (peek().kind == TokenKind::End)
                src_.fail(pos, "this 'extern \"" + language + "\"' block is "
                               "never closed");
            topLevel(*current_);
        }
        at_++;
    } else {
        topLevel(*current_);
    }

    if (c) cLinkage_--;
    return true;
}

void Parser::topLevel(Program &program) {
    // `namespace N { ... }` - a scope that qualifies what is declared in it,
    // and nothing else. Everything inside is read by this same function, so a
    // namespace nests, may be reopened, and may hold anything a file may hold.
    if (peek().is("namespace")) {
        const std::size_t pos = peek().pos;
        at_++;
        // **An unnamed namespace, opened under the name the ABI gives it.**
        if (peek().is("{")) {
            at_++;
            const std::string tag = "_GLOBAL__N_1";
            const bool outer = inUnnamedNamespace_;
            namespaceStack_.push_back(tag);
            namespaces_.insert(namespacePrefix().substr(
                                   0, namespacePrefix().size() - 2));
            inUnnamedNamespace_ = true;
            while (!peek().is("}")) {
                if (peek().kind == TokenKind::End)
                    src_.fail(pos, "this namespace never closes");
                topLevel(program);
            }
            at_++;                              // the '}'
            const std::string opened = namespacePrefix().substr(
                                           0, namespacePrefix().size() - 2);
            namespaceStack_.pop_back();
            inUnnamedNamespace_ = outer;
            // The directive is not undone at the closing brace: an unnamed
            // namespace's names answer an unqualified lookup for the rest of
            // the file, which is the whole of what it is for.
            usingNamespaces_.push_back(opened);
            return;
        }
        std::string name = expectIdent("a namespace name");
        // `namespace N::M { }` is C++17; nesting is written out here.
        if (peek().is("::"))
            src_.fail(peek().pos, "a nested namespace written 'N::M' is C++17 "
                                  "- open them one at a time");
        if (consume("=")) 
            src_.fail(pos, "a namespace alias is not supported yet");
        expect("{");
        namespaceStack_.push_back(name);
        namespaces_.insert(namespacePrefix().substr(
                               0, namespacePrefix().size() - 2));
        while (!peek().is("}")) {
            if (peek().kind == TokenKind::End)
                src_.fail(pos, "this namespace never closes");
            topLevel(program);
        }
        at_++;                                  // the '}'
        namespaceStack_.pop_back();
        return;
    }

    if (staticAssertion()) return;

    // `using namespace N;` - the names in N answer an unqualified lookup from
    // here on. A using-*declaration*, `using N::f;`, names one thing and is a
    // different rule; it is refused by name.
    if (peek().is("using")) {
        refuseAliasDeclaration();
        const std::size_t pos = peek().pos;
        at_++;
        // **`using N::f;` names one thing, and what it leaves behind is an
        // alias.**
        if (!peek().is("namespace")) {
            const bool fromGlobal = consume("::");
            std::string target = expectIdent("a name after 'using'");
            while (peek().is("::")) {
                at_++;
                target += "::" + expectIdent("a name after '::'");
            }
            expect(";");
            const std::string::size_type cut = target.rfind("::");
            if (!fromGlobal && cut == std::string::npos)
                src_.fail(pos, "a using-declaration names something declared "
                               "elsewhere, so it takes a qualified name: "
                               "'using N::f;', or 'using ::f;' for one at "
                               "global scope");
            // **Refused by name if it names nothing**, rather than recorded and
            // found missing at the use - the position of the declaration is the
            // one that says which name was meant.
            if (!hasTypeNamed(target) && !hasFunctionNamed(target) &&
                !hasGlobalNamed(target) &&
                namespaces_.find(target) == namespaces_.end())
                src_.fail(pos, "'" + target + "' is not declared, so there is "
                               "nothing here for this using-declaration to "
                               "name");
            const std::string shortName =
                cut == std::string::npos ? target : target.substr(cut + 2);
            const std::string declared = namespacePrefix() + shortName;
            if (declared != target) usingDeclarations_[declared] = target;
            return;
        }
        at_++;
        std::string opened = expectIdent("a namespace name");
        while (peek().is("::")) {
            at_++;
            opened += "::" + expectIdent("a namespace name");
        }
        expect(";");
        usingNamespaces_.push_back(opened);
        return;
    }

    if (linkageSpecification()) return;
    if (templateDeclaration()) return;

    StorageClass sc;
    Qualifiers quals;
    std::size_t scPos = peek().pos;
    const Type *base = specifiers(&sc, &quals);

    if (peek().is(";")) { at_++; return; }

    if (sc == StorageRegister)
        src_.fail(scPos, "'register' is a storage class for a local or a "
                         "parameter, and this is file scope");
    if (sc == StorageAuto)
        src_.fail(scPos, "'auto' is a storage class for a local, and this is "
                         "file scope - every object here has static duration");

    if (sc == StorageTypedef) {
        do {
            Declared td = declarator(base);
            typedefFunctionSuffix(td);
            // [dcl.typedef]/2 lets a typedef-name be redeclared to the same type,
            // which is what makes the C idiom "typedef struct S S;" legal now that the
            // tag already names the type by itself. Only a different type is an error.
            if (const Type *had = findTypedef(td.name))
                if (had != td.type)
                    src_.fail(td.pos, "'" + td.name + "' is typedefed twice, "
                                      "and not to the same type: it was '" +
                                      had->describe() + "' and is now '" +
                                      td.type->describe() + "'");
            // **A typedef at namespace scope is keyed by its qualified name**,
            // the way a class declared there already was through its tag.
            const std::string key = namespacePrefix() + td.name;
            typedefIndex_[key] = typedefs_.size();
            typedefs_.push_back(TypedefName{ key, td.type });
        } while (consume(","));
        expect(";");
        return;
    }

    locals_.clear();
    fnVars_.clear();
    scopeStarts_.clear();
    blocks_.clear();
    blockStack_.clear();
    blocks_.push_back(0);
    blockStack_.push_back(0);
    enterScope();
    frameSize_ = 0;
    Declared d = declarator(base);

    // **A `constexpr` function was refused by name until 7.5b**: accepting it as an
    // ordinary function would compile and then quietly fail to be constant where the
    // keyword was written for. The '(' still ahead tells a function from an object.
    const bool constexprFunction =
        quals.isConstexpr &&
        (peek().is("(") || d.paramsAt != 0 || d.type->isFunction());

    // **`inline` is a function specifier here.** It marks the definition
    // mergeable across translation units - a weak/COMDAT symbol - which is the
    // same treatment a template specialization already gets.
    const bool inlineFunction =
        quals.isInline &&
        (peek().is("(") || d.paramsAt != 0 || d.type->isFunction());
    if (quals.isInline && !inlineFunction)
        src_.fail(d.pos, "'inline' on a variable is a C++17 feature, and this "
                         "compiler is C++11 - 'inline' marks a function's "
                         "definition mergeable across translation units");

    // **`constexpr` does not make the return type const**, and it is measured rather
    // than reasoned: cl and clang both spell `constexpr int sq(int)` as ?sq@@YAHH@Z.
    // The keyword sets isConst because on an *object* that is exactly what it means.
    if (constexprFunction && !d.type->isFunction())
        d.type = types_.withoutConst(d.type);

    // **`int ns::f(int)` defines the `f` declared in namespace `ns`**: the qualifier is folded
    // into the name, keyed `ns::f` throughout, and the definition proceeds as one written
    // inside the namespace (the cl review's A17: "'ns' is not a class").
    if (!d.qualifier.empty() && namespaces_.find(d.qualifier) != namespaces_.end() &&
        findTypedef(d.qualifier) == nullptr) {
        d.name = d.qualifier + "::" + d.name;
        d.qualifier.clear();
    }

    // **`S g(1);` at file scope is a construction, not a prototype**: a
    // parameter list begins with a type name or is empty, the local path's own
    // question, and `S g();` is the function C++ says it is.
    bool scalarInitAhead = false;
    if (peek().is("(") && d.paramsAt == 0 && d.qualifier.empty() &&
        !d.type->isStructOrUnion() && !d.type->isArray() &&
        !d.type->isReference() && !d.type->isFunction() &&
        sc != StorageExtern && !constexprFunction && !inlineFunction) {
        scalarInitAhead = atParenInitialiser();
    }

    bool constructionAhead = scalarInitAhead;
    if (peek().is("(") && d.paramsAt == 0 && d.type->isStructOrUnion() &&
        !d.type->tag().empty() &&
        overloadsOf(constructorKey(d.type->tag())) != nullptr) {
        if (!d.qualifier.empty()) {
            // Qualified, the class answers: `S H::m(4)` defines a static member
            // only where H declares one - a member function's parameters may be
            // class-scope typedefs the scan cannot see (`string::substr` was).
            const Type *owner = findTypedef(d.qualifier);
            constructionAhead = owner != nullptr && owner->isStructOrUnion() &&
                                owner->findStaticMember(d.name) != nullptr;
        } else {
            constructionAhead = atParenInitialiser();
        }
    }

    // `int Counter::total = 0;` - a static member's definition. A member *function*'s
    // is spelled the same way up to here and told apart by the '(' that follows, which
    // is the same question the class body asks about a member.
    if (!d.qualifier.empty() && (!peek().is("(") || constructionAhead) &&
        d.paramsAt == 0 && !d.type->isFunction()) {
        // **`static` belongs to the declaration inside the class, not to this
        // definition** - [class.static.data]/2.
        if (sc == StorageStatic)
            src_.fail(scPos, "'static' can only be written on the declaration "
                             "inside '" + d.qualifier + "' - the definition of "
                             "'" + d.qualifier + "::" + d.name + "' is written "
                             "without it");
        defineStaticMember(d, program);
        return;
    }

    if (d.type->isFunction() && d.paramsAt == 0 && !peek().is("(")) {
        std::vector<const Type *> ps(d.type->params());
        declareFunction(d.name, d.type->returns(), ps,
                        d.type->isVariadicFn(), false, d.pos,
                        sc == StorageStatic);
        if (peek().is("{"))
            src_.fail(d.pos, "'" + d.name + "' cannot be *defined* through a "
                             "typedef - the body has no names for the "
                             "parameters; write the parameter list out");
        expect(";");
        return;
    }

    if ((!peek().is("(") || constructionAhead) && d.paramsAt == 0) {
        const Type *deducedSoFar = nullptr;
        for (;;) {
            if (mentionsDeduced(d.type)) {
                d.type = deduceAuto(d.type, d.name, d.pos);
                checkOneDeducedType(deducedSoFar, lastDeducedAuto_, d.name,
                                    d.pos);
            }
            if (d.type->isVoid()) src_.fail(d.pos, "'" + d.name + "' cannot have type void");
            // **A reference at file scope** holds a pointer, so its storage is
            // one; bound in the image where the initialiser is a global's
            // address and in the init function otherwise.
            if (d.type->isReference()) {
                const std::string gname =
                    (namespaceStack_.empty() || cLinkage_ > 0)
                        ? d.name : namespacePrefix() + d.name;
                GlobalSym *prev = findGlobalToUpdate(gname);
                if (prev != nullptr && prev->type != d.type)
                    src_.fail(d.pos, "'" + d.name + "' was already declared as '" +
                                     prev->type->describe() + "', not '" +
                                     d.type->describe() + "'");
                const bool internal = internalLinkage(sc);
                refuseVolatileWithLinkage(quals.isVolatile, internal, d.pos);
                const std::string symbol = prev != nullptr
                    ? prev->symbol : dataSymbol(gname, d.type, internal, d.pos);
                const bool defines = !(sc == StorageExtern && !peek().is("="));
                std::vector<GlobalPiece> pieces;
                bool hasInit = false;
                if (defines) {
                    if (prev != nullptr && prev->emitted)
                        src_.fail(d.pos, "'" + d.name + "' is defined twice");
                    const FunctionState outer = enterInitFunction();
                    bindStaticReference(d, symbol, pieces, hasInit, &dynInit_);
                    leaveInitFunction(outer);
                }
                if (prev != nullptr) {
                    if (defines) { prev->emitted = true; prev->hasInit = true; }
                } else {
                    globalIndex_[gname] = globals_.size();
                    globals_.push_back(GlobalSym{ gname, symbol, d.type, false,
                                                  defines, defines, false, 0 });
                }
                if (defines)
                    program.globals.push_back(Global{ gname, symbol,
                                                      types_.pointerTo(d.type->referent()),
                                                      std::move(pieces), hasInit,
                                                      internal, false });
                if (!consume(",")) break;
                d = declarator(base);
                continue;
            }

            // **An array of a class with a constructor** at file scope goes
            // the way one object does: built before main by the class's loop,
            // destroyed at exit by a helper - buildStaticArrayConstruction.
            const Type *arrayClass = nullptr;
            {
                const Type *elem = d.type;
                while (elem->isArray()) elem = elem->pointee();
                const Type *plain = elem->unqualified();
                if (d.type->isArray() && plain->isStructOrUnion() &&
                    !plain->tag().empty() &&
                    overloadsOf(constructorKey(plain->tag())) != nullptr)
                    arrayClass = plain;
            }

            // **A class with a constructor, at file scope.** This path had no test at
            // all, so the object was laid out as bytes and the constructor never ran.
            // The braced form is asked first: C++11 makes such a class no aggregate.
            if (arrayClass != nullptr ||
                (d.type->isStructOrUnion() && !d.type->tag().empty())) {
                const bool braced = peek().is("=") && peekAt(1).is("{");
                if (braced && arrayClass == nullptr && hasMemberInitialiser(d.type->tag()))
                    src_.fail(d.pos, "'" + d.type->describe() + "' writes an "
                                     "initialiser on a member, so in C++11 it "
                                     "is not an aggregate and a braced list "
                                     "cannot initialise it - C++14 changed "
                                     "that rule and this compiler is C++11");
                // **Built before main**, [basic.start.init]/2, in the init
                // function and in declaration order; destroyed at exit in
                // reverse. `extern S s;` alone declares and builds nothing.
                if ((arrayClass != nullptr ||
                     overloadsOf(constructorKey(d.type->tag())) != nullptr) &&
                    sc != StorageExtern) {
                    const std::string gname =
                        (namespaceStack_.empty() || cLinkage_ > 0)
                            ? d.name : namespacePrefix() + d.name;
                    GlobalSym *prev = findGlobalToUpdate(gname);
                    if (prev != nullptr &&
                        prev->type->unqualified() != d.type->unqualified())
                        src_.fail(d.pos, "'" + d.name + "' was already declared "
                                         "as '" + prev->type->describe() +
                                         "', not '" + d.type->describe() + "'");
                    if (prev != nullptr && prev->emitted)
                        src_.fail(d.pos, "'" + d.name + "' is defined twice");
                    const bool internal = internalLinkage(sc) || d.type->isConst();
                    refuseVolatileWithLinkage(quals.isVolatile, internal, d.pos);
                    const std::string symbol = prev != nullptr
                        ? prev->symbol
                        : dataSymbol(gname, d.type, internal, d.pos);
                    // The Microsoft helper is scoped innermost first:
                    // ??__Fmg@M@N@@YAXXZ for N::M::mg, measured.
                    std::string helper;
                    if (target_.microsoftNames()) {
                        std::vector<std::string> parts;
                        std::size_t from = 0;
                        for (;;) {
                            const std::size_t at = gname.find("::", from);
                            parts.push_back(gname.substr(from, at == std::string::npos
                                                               ? at : at - from));
                            if (at == std::string::npos) break;
                            from = at + 2;
                        }
                        std::string scoped;
                        for (std::size_t i = parts.size(); i-- > 0; )
                            scoped += parts[i] + "@";
                        helper = atexitHelperName(scoped);
                    }
                    dynamicInitialise(d, symbol, helper, false);
                    if (prev != nullptr) {
                        prev->emitted = true;
                        prev->hasInit = true;
                    } else {
                        globalIndex_[gname] = globals_.size();
                        globals_.push_back(GlobalSym{ gname, symbol, d.type,
                                                      d.type->isConst(), true,
                                                      true, false, 0 });
                    }
                    // Not isConst: the constructor writes it, so it cannot
                    // be laid down read-only.
                    program.globals.push_back(Global{ gname, symbol, d.type,
                                                      std::vector<GlobalPiece>(),
                                                      false, internal, false });
                    program.globals.back().align = quals.alignAs;
                    if (!consume(",")) break;
                    d = declarator(base);
                    continue;
                }
            }

            std::vector<GlobalPiece> pieces;
            bool hasInit = false;
            // Read while the initialiser tree is still in scope - `in` does
            // not outlive the branch, and flattenInit answers in bytes rather
            // than in the value this wants.
            bool constantKnown = false;
            long long constantValue = 0;
            // The floating twin of the same read-back: a `const double` keeps
            // what it is worth so one defined from it can fold.
            bool constantDoubleKnown = false;
            long double constantDoubleValue = 0;
            Init dynamicScalar;
            if (scalarInitAhead || consume("=") || atBracedInitialiser(d.name)) {
                Init in = scalarInitAhead ? parenthesisedInitialiser(d)
                                          : parseInitialiser();
                scalarInitAhead = false;
                if (d.type->isArray() && d.type->length() < 0)
                    d.type = types_.arrayOf(d.type->pointee(),
                                            inferredLength(in, d.type->pointee(), d.pos));
                constantKnown = constantInitialiser(d.type, in, &constantValue);
                if (quals.isConst && d.type->isFloating() && !in.isList &&
                    in.value != nullptr) {
                    bool p53 = false, x87 = false;
                    long double dv = 0;
                    if (foldDouble(*in.value, target_, &dv, &p53, &x87)) {
                        constantDoubleValue = dv;
                        constantDoubleKnown = true;
                    }
                }
                if (!constantKnown && !constantDoubleKnown && quals.isConstexpr)
                    src_.fail(d.pos, "'" + d.name + "' is 'constexpr', so its "
                                     "value has to be known while this is "
                                     "compiled, and this initialiser is not a "
                                     "constant expression");
                // **Dynamic initialisation of a scalar or a trivial class** (A12,
                // A13): `int gA = init(1);` and `P g = P();` are stored before main
                // in declaration order, as a constructor runs there; the object is .bss.
                if (staticallyInitialisable(d.type, in)) {
                    flattenInit(d.type, in, 0, pieces);
                    hasInit = true;
                } else {
                    dynamicScalar = std::move(in);
                }
            } else if (quals.isConstexpr && sc != StorageExtern) {
                src_.fail(d.pos, "'" + d.name + "' is 'constexpr' and has no "
                                 "initialiser - there is nothing for it to be");
            } else if (d.type->isConst() && sc != StorageExtern) {
                // The same [dcl.init]/7 the local path asks about. `extern` is
                // exempt because it declares rather than defines: the definition
                // is somewhere else and is where the initialiser has to be.
                requireConstInitialised(d.type, d.name, d.pos);
            } else if (d.type->isArray() && d.type->length() < 0 &&
                       sc != StorageExtern) {
                src_.fail(d.pos, "'" + d.name + "' has no length and no initialiser "
                                 "to take one from");
            }

            // **Redeclaration is asked about under the key the definition will
            // use.**
            const std::string gname =
                (namespaceStack_.empty() || cLinkage_ > 0)
                    ? d.name : namespacePrefix() + d.name;
            if (GlobalSym *prev = findGlobalToUpdate(gname)) {
                const Type *both = composite(prev->type, d.type);
                if (both == nullptr)
                    src_.fail(d.pos, "'" + d.name + "' was already declared as '" +
                                     prev->type->describe() + "', not '" +
                                     d.type->describe() + "'");

                prev->type = both;
                d.type = both;
                if ((hasInit || dynamicScalar.value != nullptr) && prev->hasInit)
                    src_.fail(d.pos, "'" + d.name + "' is given an initialiser twice");
                if (hasInit || dynamicScalar.value != nullptr) prev->hasInit = true;
                if (dynamicScalar.value != nullptr)
                    dynamicInitialiseScalar(gname, d.type, dynamicScalar);

                // `extern` with an initialiser defines - [dcl.stc]/6 - and
                // keeps external linkage a const object would otherwise lose.
                if (sc != StorageExtern || hasInit || dynamicScalar.value != nullptr) {
                    if (!prev->emitted) {
                        prev->emitted = true;
                        program.globals.push_back(Global{ d.name, prev->symbol,
                                                          d.type, pieces, hasInit,
                                                          internalLinkage(sc),
                                                          prev->isConst });
                    } else {
                        for (Global &g : program.globals)
                            if (g.name == d.name) {
                                g.type = both;
                                if (hasInit) { g.init = pieces; g.hasInit = true; }
                                break;
                            }
                    }
                }
                if (!consume(",")) break;
                d = declarator(base);
                continue;
            }

            // A variable declared in a namespace is keyed and mangled by its qualified
            // name, the same as a function. `extern "C"` does not reach into one, so a
            // name with C linkage keeps what it was written with - `gname` above.
            globalIndex_[gname] = globals_.size();
            bool objectIsConst = d.type->isConst();
            // A const object at namespace scope has internal linkage of its own -
            // [basic.link]/3 - which is why a header may define one and C, where it
            // would be external, may not. Nothing outside can name it.
            bool internal = sc == StorageStatic ||
                            (objectIsConst && sc != StorageExtern);
            refuseVolatileWithLinkage(quals.isVolatile, internal, d.pos);
            std::string symbol = dataSymbol(gname, d.type, internal, d.pos);
            const bool dynamic = dynamicScalar.value != nullptr;
            globals_.push_back(GlobalSym{ gname, symbol, d.type, objectIsConst,
                                          sc != StorageExtern || hasInit || dynamic,
                                          hasInit || dynamic,
                                          constantKnown, constantValue });
            globals_.back().isConstantDouble = constantDoubleKnown;
            globals_.back().constantDouble = constantDoubleValue;
            if (dynamic) dynamicInitialiseScalar(gname, d.type, dynamicScalar);
            if (sc != StorageExtern || hasInit || dynamic) {
                program.globals.push_back(Global{ gname, symbol, d.type,
                                                  std::move(pieces), hasInit,
                                                  internal, objectIsConst && !dynamic });
                program.globals.back().align = quals.alignAs;
                refuseWeakAlignas(quals.alignAs, d.type, d.pos);
            }
            if (!consume(",")) break;
            d = declarator(base);
        }
        expect(";");
        return;
    }

    // **A trailing return type is C++11, and it arrives here wearing the same `auto`.**
    // `auto f(int) -> int` says what the return type is rather than asking for it to be
    // deduced. The parameter list is still ahead, so the arrow is found past it.
    bool trailingArrow = false;
    if (peek().is("(")) {
        int depth = 0;
        for (std::size_t i = at_; i < tokens_.size(); i++) {
            if (tokens_[i].is("(")) depth++;
            else if (tokens_[i].is(")")) {
                if (--depth == 0) {
                    trailingArrow = i + 1 < tokens_.size() &&
                                    tokens_[i + 1].is("->");
                    break;
                }
            }
        }
    }
    if (mentionsDeduced(d.type) && trailingArrow)
        src_.fail(d.pos, "a trailing return type - `auto f(...) -> T` - is "
                         "C++11 and is not supported yet; write the return "
                         "type in front, which says the same thing wherever it "
                         "does not name a parameter");
    if (mentionsDeduced(d.type))
        src_.fail(d.pos, "a function's return type cannot be deduced - `auto` "
                         "there is C++14, and this compiler is C++11");

    std::size_t resumeAt = 0;
    if (d.paramsAt != 0) {
        resumeAt = at_;
        at_ = d.paramsAt;
    }

    expect("(");
    std::vector<const Type *> params;
    std::vector<const Type *> written;
    std::vector<Param> paramSlots;
    bool variadic = false;
    std::size_t aliveParams = 0;

    // **`this` is parameter zero, and it is declared before any written one so that it
    // takes the first slot.** That is the whole of how a member function differs at the
    // machine. It is not in `params`, which is the declared signature.
    const Type *memberOf = nullptr;
    if (!d.qualifier.empty()) {
        memberOf = findTypedef(d.qualifier);
        if (memberOf == nullptr || !memberOf->isStructOrUnion())
            src_.fail(d.pos, "'" + d.qualifier + "' is not a class");
        // `void S::f()` written inside `namespace N` defines `N::S::f`, and every table
        // downstream is keyed by the qualified tag. Take the name the class was found
        // under rather than the one that was written.
        d.qualifier = memberOf->tag();
        currentClass_ = memberOf;
    }

    std::vector<std::size_t> defaults;
    if (!consume(")")) {
        if (peek().is("void") && peekAt(1).is(")")) {
            at_ += 2;
        } else {
            for (;;) {
                if (consume("...")) { variadic = true; expect(")"); break; }

                // `Ts... rest` in a definition: as many parameters as the
                // pack has members, each with a name of its own, and those
                // names are what `rest...` expands to at a call.
                {
                    std::vector<const Type *> packTypes;
                    std::vector<std::string> packNames;
                    if (packParameter(&packTypes, &packNames)) {
                        for (std::size_t k = 0; k < packTypes.size(); k++) {
                            inParams_ = true;
                            int poff = declare(packNames[k], packTypes[k],
                                               peek().pos);
                            inParams_ = false;
                            params.push_back(types_.withoutConst(packTypes[k]));
                            paramSlots.push_back(Param{ packTypes[k], poff });
                        }
                        if (consume(")")) break;
                        expect(",");
                        continue;
                    }
                }

                std::size_t pscPos = peek().pos;
                StorageClass psc;
                Qualifiers pquals;
                const Type *pt = specifiers(&psc, &pquals);
                if (psc != StorageNone && psc != StorageRegister)
                    src_.fail(pscPos, "'register' is the only storage class a "
                                      "parameter may have");
                Declared pd = declarator(pt, true);
                if (mentionsDeduced(pd.type))
                    src_.fail(pd.pos, "a parameter's type cannot be deduced - "
                                      "`auto` there is C++14, and this "
                                      "compiler is C++11");
                const Type *declared = pd.type;
                if (pd.type->isArray())
                    pd.type = types_.pointerTo(pd.type->pointee());

                // **A class whose copy is a constructor call arrives by address**, on
                // both ABIs and whatever its size. The parameter is lowered to a
                // reference; the declared type is untouched, and the caller owns it.
                const bool byAddress = passedByAddress(pd.type);
                const Type *held = byAddress ? types_.referenceTo(pd.type)
                                             : pd.type;
                int off;
                {
                    // **A definition may leave a parameter unnamed** - C++ does not require one
                    // where C did, and `operator++(int)` is written that way by everybody: the
                    // parameter exists only to tell the postfix form from the prefix one.
                    if (pd.name.empty()) {
                        if (pd.type->isVoid())
                            src_.fail(pd.pos,
                                      "'void' is only a parameter list on its own");
                        pd.name = "$unnamed" + std::to_string(params.size());
                    }
                    inParams_ = true;
                    off = declare(pd.name, held, pd.pos);
                    inParams_ = false;
                    locals_.back().isConst = pd.type->isConst();
                    locals_.back().isRegister = (psc == StorageRegister);
                    locals_.back().byValueByAddress = byAddress;

                    // **On Microsoft the callee destroys its by-value class
                    // parameter**, in a register or by address alike - measured with
                    // cl. Itanium puts it on the caller, where the temporary is made.
                    if (target_.microsoftNames() && pd.type->isStructOrUnion() &&
                        pd.type->hasDestructor()) {
                        alive_.push_back(Alive{ pd.name, off,
                                                pd.type->unqualified(),
                                                byAddress });
                        aliveParams++;
                    }
                }
                params.push_back(types_.withoutConst(pd.type));
                written.push_back(parameterAsWritten(declared, params.back()));
                paramSlots.push_back(Param{ held->isReference()
                                            ? types_.pointerTo(held->referent())
                                            : held, off });
                // A default written on the *definition*. The parameter list here is
                // read by this loop and not by parameterTypes, so the same recording
                // happens twice, and the same way: a place in the token stream.
                defaults.resize(params.size(), 0);
                if (consume("=")) {
                    if (peek().is("{"))
                        src_.fail(peek().pos, "a braced default argument is not "
                                              "supported yet - write the value");
                    defaults.back() = at_;
                    skipDefaultArgument();
                }
                if (consume(")")) break;
                expect(",");
            }
        }
    }
    if (resumeAt != 0) at_ = resumeAt;

    // Hand what this parameter list collected to whichever declare() runs below, the
    // same way parameterTypes hands over its own. **Before the prototype branch and
    // not after it**: that branch returns as soon as it has declared the function.
    writtenParams_.clear();
    writtenFor_.clear();
    if (written.size() == params.size() && written != params) {
        writtenParams_ = written;
        writtenFor_ = params;
    }
    bool sawDefault = false;
    for (std::size_t i = 0; i < defaults.size(); i++)
        if (defaults[i] != 0) sawDefault = true;
    if (sawDefault) {
        requireDefaultsAreASuffix(defaults, d.pos);
        pendingDefaults_ = defaults;
    }


    if (peek().is("(") || peek().is("[")) {
        bool fn = peek().is("(");
        src_.fail(peek().pos,
                  std::string("a function cannot return ") +
                  (fn ? "a function" : "an array") +
                  " - it may return a pointer to one, written '" +
                  (fn ? "int (*f(void))(void)" : "int (*f(void))[3]") + "'");
    }

    // A member function's constness is written after the parameter list, and
    // it is part of which member this is - Point::get() const and
    // Point::get() are two functions.
    bool constThis = false;
    if (memberOf != nullptr && consume("const")) constThis = true;
    // The same C++11 rule the class body applies.
    if (memberOf != nullptr && constexprFunction) constThis = true;

    // The exception specification comes after the constness, which is the order C++ writes them in.
    pendingNoexcept_ = exceptionSpecification();
    // **Captured here because declareFunction consumes it.**
    const bool declaredNoexcept = pendingNoexcept_;

    if (consume(";")) {
        if (memberOf != nullptr)
            src_.fail(d.pos, "'" + d.qualifier + "::" + d.name + "' is declared "
                             "inside the class - this says it again outside, "
                             "which declares nothing new");
        declareFunction(d.name, d.type, params, variadic, false, d.pos,
                        sc == StorageStatic);
        // A prototype's by-value parameters were registered alive above, and
        // there is no body to destroy them: pop them, or the next definition does.
        alive_.resize(alive_.size() - aliveParams);
        return;
    }
    const Signature *member = nullptr;
    if (memberOf != nullptr) {
        std::string key = d.qualifier + "::" + d.name;   // "Point::~Point" too
        // **A member function template specialization declares itself here.**
        if (memberTemplateInst_ && d.name == memberTemplateOf_) {
            key = d.qualifier + "::" + memberTemplateName_;
            if (overloadsOf(key) == nullptr) {
                const Type *fnType =
                    types_.functionType(d.type, params, variadic);
                const char code = memberTemplateAccess_ == Access::Public ? 'Q'
                                : memberTemplateAccess_ == Access::Protected ? 'I'
                                                                             : 'A';
                std::string sym, why;
                const bool ok = target_.microsoftNames()
                    ? microsoftMemberTemplateName(d.qualifier, memberOf, d.name,
                          fnType, memberTemplateArgs_, code, constThis, &sym, &why)
                    : itaniumMemberTemplateName(d.qualifier, memberOf, d.name,
                          fnType, memberTemplateArgs_, constThis, &sym, &why);
                if (!ok)
                    src_.fail(d.pos, "'" + key + "' cannot be given a name the "
                                     "linker can hold: " + why);
                // **Under two keys, as a free specialization is.**
                functionIndex_[d.qualifier + "::" + d.name]
                    .push_back(functions_.size());
                functionIndex_[key].push_back(functions_.size());
                functions_.push_back(Signature{
                    memberTemplateName_, sym, d.type, params, variadic, false,
                    d.pos, false, d.qualifier, constThis,
                    memberTemplateAccess_, false });
                functions_.back().fromTemplate = true;
                // **The defaults its parameter list just read.**
                if (!pendingDefaults_.empty()) {
                    defaultArgs_[sym] = pendingDefaults_;
                    defaultArgNamespace_[sym] = namespaceStack_;
                }
            }
        }
        if (const std::vector<std::size_t> *set = overloadsOf(key)) {
            for (std::size_t k = 0; k < set->size() && member == nullptr; k++) {
                const Signature &f = functions_[(*set)[k]];
                if (f.constThis == constThis && sameParameters(f.params, params))
                    member = &f;
            }
        }
        if (member == nullptr)
            src_.fail(d.pos, "'" + d.qualifier + "' declares no member '" +
                             d.name + "' with these parameters");
        if (member->returns != d.type)
            src_.fail(d.pos, "'" + key + "' was declared to return '" +
                             member->returns->describe() + "' and this says '" +
                             d.type->describe() + "'");
        if (member->defined)
            src_.fail(d.pos, "'" + key + "' is defined twice");
        // **This used to write `member->pos` into the *first* overload's entry.**
        const_cast<Signature *>(member)->defined = true;

        // **A static member's body gets no `this` slot**, which is the whole
        // of what makes it static once the name is settled.
        inStaticMember_ = member->isStaticMember;
        msVbInitSlot_ = -1;
        if (!member->isStaticMember) {
            // `this` takes the first slot, and its type carries the constness the member
            // was declared with - so a const member function cannot write through it, by
            // the ordinary rule that a const object's members are const.
            const Type *pointee = constThis ? types_.withConst(memberOf) : memberOf;
            const Type *thisType = types_.pointerTo(pointee);
            inParams_ = true;
            thisOffset_ = declare("this", thisType, d.pos);
            inParams_ = false;
            paramSlots.insert(paramSlots.begin(), Param{ thisType, thisOffset_ });
            // **Itanium's VTT, second**: a C2 or D2 of a class with virtual
            // bases is handed the tables its vptrs come from - [2.6.2].
            vttSlot_ = -1;
            if (takesVtt(memberOf) && (d.name == localOf(d.qualifier) ||
                                       d.name == "~" + localOf(d.qualifier))) {
                inParams_ = true;
                vttSlot_ = declare(".vtt", vttType(), d.pos);
                inParams_ = false;
                paramSlots.insert(paramSlots.begin() + 1,
                                  Param{ vttType(), vttSlot_ });
            }
            // **cl's hidden most-derived flag, last of all.**
            msVbInitSlot_ = -1;
            if (target_.microsoftNames() && memberOf->hasVirtualBase() &&
                d.name == localOf(d.qualifier)) {
                inParams_ = true;
                msVbInitSlot_ = declare(".initVBases", types_.intType(), d.pos);
                inParams_ = false;
                paramSlots.push_back(Param{ types_.intType(), msVbInitSlot_ });
            }
        }
    } else {
        inStaticMember_ = false;
        msVbInitSlot_ = -1;
        vttSlot_ = -1;
        declareFunction(d.name, d.type, params, variadic, true, d.pos,
                        sc == StorageStatic);
        // Which function's body is about to be read, so that an access check inside it
        // can ask whether a class befriended *this* function. A member's is left empty:
        // the qualified form that would befriend a member is refused where written.
        currentFunction_ = lookupSignature(d.name, params, variadic, d.pos).symbol;
    }
    // Set for a member's body too, unlike the friend question above, because a local
    // class inside a member function is spelled by wrapping that function's name.
    // **Taken by value**: `member` points into a vector any declaration can move.
    std::string definedSymbol;
    if (member != nullptr) {
        definedSymbol = member->symbol;
        currentFunction_ = definedSymbol;
    }
    currentFunctionName_ = d.name;
    localTypes_.clear();
    // Closures are numbered within the function that writes them, which is what clang does.
    lambdaCount_ = 0;
    // The mem-initializer list, [class.base.init], parsed here because `this` and the parameters are in scope and the body has not begun.
    std::vector<StmtPtr> memberInits;
    // Which members this constructor's own list covers. Kept out here because
    // the initialisers the class wrote are applied to the rest, below, and the
    // list itself is scoped to the block that reads it.
    std::set<std::string> namedInInit;
    std::map<std::string, std::vector<ExprPtr> > baseArgs;
    std::map<std::string, std::vector<ExprPtr> > memberExprs;
    std::map<std::string, std::size_t> where;
    // The members written `: m()`, and for one with a constructor the index of the one
    // to run; functions_.size() says there is none. Out here with the other two: the
    // declaration-order walk below reads all three from outside that block.
    std::map<std::string, std::size_t> valueInit;
    const bool isCtor = memberOf != nullptr && d.name == localOf(d.qualifier);
    if (memberOf != nullptr && peek().is(":")) {
        if (!isCtor)
            src_.fail(peek().pos, "an initialiser list belongs to a "
                                  "constructor, and '" + d.name + "' is not one");
        at_++;
        for (;;) {
            std::size_t epos = peek().pos;
            // **A base written as a template-id - `: P<int>(x)` - is read as a
            // type**, the way the base-clause reads it; expectIdent stopped at
            // the `<` with "expected '('". A member is never spelt with one.
            const Type *templateBase = nullptr;
            std::string entry;
            {
                std::size_t k = 0;
                while (peekAt(k).kind == TokenKind::Ident && peekAt(k + 1).is("::"))
                    k += 2;
                if (peekAt(k).kind == TokenKind::Ident && peekAt(k + 1).is("<") &&
                    atTypeName()) {
                    StorageClass bsc;
                    Qualifiers bquals;
                    templateBase = specifiers(&bsc, &bquals);
                    if (templateBase != nullptr) templateBase = templateBase->unqualified();
                    entry = templateBase != nullptr ? templateBase->tag()
                                                    : peekAt(k).text;
                }
            }
            if (templateBase == nullptr) {
                entry = expectIdent("a member or base to initialise");
                // **A base may be named with its namespace** - `:
                // cc::Lowering(...)` is how a class writes it when the base is not
                // in scope unqualified.
                while (peek().is("::") && peekAt(1).kind == TokenKind::Ident) {
                    at_++;
                    entry += "::" + expectIdent("a name after '::'");
                }
            }
            expect("(");
            const int frameBeforeArgs = frameSize_;
            std::vector<ExprPtr> args;
            parseArguments(args);

            // **The name written is not always the base's tag.** A class in a
            // namespace has a qualified tag - `n::Base` - and the list names
            // it the way the source can see it, `Base`.
            bool isBase = false;
            bool wasVirtualBase = false;
            std::string baseKey = entry;
            const Type *namedBase = templateBase != nullptr ? templateBase
                                                            : findTypedef(entry);
            const std::vector<Type::BaseSpec> &bs = memberOf->bases();
            for (std::size_t i = 0; i < bs.size(); i++)
                // **And the injected class name**: inside a derived class the base is `Base`,
                // whatever namespace it was declared in, so `: Base(v)` for a `cc::Base` is the
                // ordinary spelling and was refused as neither a member nor a base.
                if (bs[i].type->tag() == entry ||
                    bs[i].type->localName() == entry ||
                    (namedBase != nullptr &&
                     namedBase->unqualified() == bs[i].type->unqualified())) {
                    isBase = true;
                    baseKey = bs[i].type->tag();
                    wasVirtualBase = bs[i].isVirtual;
                    break;
                }

            if (isBase) {
                if (baseArgs.count(baseKey))
                    src_.fail(epos, "'" + entry + "' is initialised twice");
                // **A virtual base is built in C1, not here**, so its
                // arguments are emitted into a body with a frame of its own.
                if (wasVirtualBase && frameSize_ != frameBeforeArgs)
                    src_.fail(epos, "'" + entry + "(...)' initialises a virtual "
                                    "base with an argument that needs a "
                                    "temporary of its own, and that is not "
                                    "supported yet - a virtual base is built by "
                                    "the most-derived constructor, in a body "
                                    "whose frame this temporary is not in. An "
                                    "argument made of the parameters and "
                                    "constants works");
                baseArgs[baseKey] = std::move(args);
            } else if (const Member *m = memberOf->findMember(entry)) {
                if (memberExprs.count(entry))
                    src_.fail(epos, "'" + entry + "' is initialised twice");
                if (m->type->isConst())
                    src_.fail(epos, "a const member in an initialiser list is "
                                    "not supported yet");
                // **`: m()` value-initialises the member** - [class.base.init] hands
                // the empty pair to [dcl.init]/8: a scalar zeroes, a class with no
                // constructor zeroes leaf by leaf, a user-provided one runs alone.
                if (args.empty()) {
                    if (m->type->isReference())
                        src_.fail(epos, "'" + entry + "()' would leave a "
                                        "reference member bound to nothing - "
                                        "give it what it refers to");
                    const Type *mt = m->type->unqualified();
                    const Type *mc = mt;
                    while (mc->isArray()) mc = mc->pointee();
                    mc = mc->isStructOrUnion() ? mc->unqualified() : nullptr;
                    std::size_t ctorIndex = functions_.size();
                    if (mc != nullptr && !mc->tag().empty() &&
                        overloadsOf(constructorKey(mc->tag())) != nullptr) {
                        if (mt->isArray())
                            src_.fail(epos, "'" + entry + "()' would run '" +
                                            mc->describe() + "''s constructor "
                                            "once per element, which an "
                                            "initialiser list cannot say yet");
                        const Signature *ctor = defaultConstructorOf(mc);
                        if (ctor == nullptr)
                            src_.fail(epos, "'" + entry + "()' needs a "
                                            "constructor of '" + mc->describe() +
                                            "' taking nothing, and it has none");
                        if (ctor->access != Access::Public &&
                            !insideAccessOf(mc, ctor->access) &&
                            !isFriendOf(mc))
                            src_.fail(epos, "'" + entry + "()' would call a " +
                                            std::string(ctor->access == Access::Private
                                                        ? "private" : "protected") +
                                            " constructor of '" + mc->describe() +
                                            "'");
                        ctorIndex = static_cast<std::size_t>(ctor - &functions_[0]);
                        functions_[ctorIndex].used = true;
                    }
                    valueInit[entry] = ctorIndex;
                }
                // **How many values are too many is not a question this loop can
                // answer any more.** A class-typed member takes as many as one of its
                // constructors does, so the check moved down with the construction.
                memberExprs[entry] = std::move(args);
                namedInInit.insert(entry);
                where[entry] = epos;
            } else if (entry == d.qualifier) {
                src_.fail(epos, "a delegating constructor is not supported "
                                "yet - it is C++11's own addition and comes "
                                "later");
            } else {
                src_.fail(epos, "'" + entry + "' is neither a member of '" +
                                d.qualifier + "' nor a direct base of it");
            }
            if (!consume(",")) break;
        }

    }

    // **Every member, in declaration order, by the first of three rules that applies to
    // it** - [class.base.init]/8, /9 and /11: named in the list, its own initialiser, or
    // a class with constructors default-constructed. A union's members are not built.
    if (memberOf != nullptr && isCtor) {
        const std::vector<Member> &all = memberOf->members();
        for (std::size_t i = 0; i < all.size(); i++) {
            const Member *m = &all[i];
            std::map<std::string, std::vector<ExprPtr> >::iterator found =
                memberExprs.find(m->name);
            if (found == memberExprs.end()) {
                // A base's members are in this class's list too - see the walk
                // in synthesizeDefaultCtor. The base's constructor built them.
                if (memberFromBase(memberOf, *m)) continue;
                StmtPtr one = memberInitialiser(d.qualifier, memberOf, *m,
                                                thisOffset_, d.pos);
                std::vector<ExprPtr> none;
                if (one == nullptr && memberOf->kind() != Kind::Union)
                    one = constructMember(d.qualifier, memberOf, *m,
                                          thisOffset_, none, d.pos, false);
                if (one != nullptr) memberInits.push_back(std::move(one));
                continue;
            }
            std::size_t epos = where[m->name];

            // **An empty pair belongs to value-initialisation, and only to it.**
            const bool valueInitialised = valueInit.count(m->name) != 0;

            if (!m->type->isReference() && !valueInitialised) {
                StmtPtr built = constructMember(d.qualifier, memberOf, *m,
                                                thisOffset_, found->second,
                                                epos, false);
                if (built != nullptr) {
                    memberInits.push_back(std::move(built));
                    continue;
                }
            }
            // Anything that is neither constructed nor value-initialised takes
            // one value and assigns it, or binds it where the member is a
            // reference.
            if (!valueInitialised && found->second.size() != 1)
                src_.fail(epos, "'" + m->name + "' takes one value here, "
                                "given " + std::to_string(found->second.size()));

            ExprPtr field = thisMember(thisOffset_, memberOf, *m);

            // **A reference member is bound, not assigned**, and this is the one place
            // it can be. What the slot holds is an address, so the member is typed as
            // the pointer it really is and `bindReference` supplies that address.
            if (m->type->isReference()) {
                const Type *held = types_.pointerTo(m->type->referent());
                field->setType(held);
                ExprPtr addr = bindReference(m->type, std::move(found->second[0]),
                                             epos, "'" + m->name + "'");
                ExprPtr bind(new Assign(std::move(field), std::move(addr)));
                bind->setType(held);
                memberInits.push_back(StmtPtr(new ExprStmt(std::move(bind))));
                continue;
            }
            field->setType(m->type);

            std::map<std::string, std::size_t>::const_iterator vi =
                valueInit.find(m->name);
            if (vi != valueInit.end()) {
                const Type *mt = m->type->unqualified();
                const bool hasCtor = vi->second != functions_.size();
                // The zeroing first, cloned from the member's own access so
                // every store lands on the member; then the constructor, on
                // the member's address, when there is one to run.
                if (!hasCtor || functions_[vi->second].implicit)
                    if (ExprPtr chain = zeroChain(*field, mt))
                        memberInits.push_back(StmtPtr(new ExprStmt(std::move(chain))));
                if (hasCtor) {
                    const Type *ptr = types_.pointerTo(mt);
                    ExprPtr addr(new Unary('&', std::move(field)));
                    addr->setType(ptr);
                    std::vector<ExprPtr> one;
                    one.push_back(std::move(addr));
                    std::vector<const Type *> ps;
                    ps.push_back(ptr);
                    memberInits.push_back(StmtPtr(new ExprStmt(
                        completeCall(mt->tag(), functions_[vi->second].symbol,
                                     nullptr, types_.get(Kind::Void), ps, false,
                                     epos, std::move(one)))));
                }
                continue;
            }

            ExprPtr value = decay(std::move(found->second[0]));
            checkAssignable(*value, m->type, epos, "'" + m->name + "'");
            value = convert(std::move(value), m->type);
            ExprPtr assign(new Assign(std::move(field), std::move(value)));
            assign->setType(m->type);
            memberInits.push_back(StmtPtr(new ExprStmt(std::move(assign))));
        }
    }

    returnType_ = d.type;
    functionName_ = d.name;
    staticSymbols_.clear();

    // A member function is the second argument: on the Microsoft ABI the
    // hidden pointer serves every class a *non-static* member returns,
    // whatever its size.
    const bool returnsAsMember = memberOf != nullptr && !inStaticMember_;
    int sretSlot = 0;
    if (d.type->isStructOrUnion() && returnsIndirectly(d.type, returnsAsMember)) {
        frameSize_ += 8;
        frameSize_ = alignTo(frameSize_, 8);
        sretSlot = frameSize_;
    }

    int regSaveSlot = 0;
    if (variadic) {
        frameSize_ = alignTo(frameSize_, 16);
        frameSize_ += 176;
        regSaveSlot = frameSize_;
    }
    variadicBody_ = variadic;

    // Anything already alive belongs to an enclosing function.
    const std::size_t paramsFrom = alive_.size() - aliveParams;

    // **A function try block wraps the whole body, mem-initialisers
    // included** - [except.pre] - so a handler for it catches what a base or
    // member constructor threw, which no region built here can express.
    if (peek().is("try"))
        src_.fail(peek().pos, "a function try block - 'int f() try { } "
                              "catch (...) { }' - is not supported yet: its "
                              "handler covers the mem-initialisers as well as "
                              "the body, so a 'try' inside the body is not the "
                              "same thing");

    atFunctionBody_ = true;
    // [except.spec]/9 needs this inside the body, and the declarator read it a
    // few hundred lines up.
    inNoexceptFunction_ = declaredNoexcept;
    bodyCleanupFrom_ = paramsFrom;
    // A class can be defined inside a function and its members defined from
    // there, so what this function allocates is what it added to the list.
    const std::size_t guardsFrom = guardSlots_.size();
    StmtPtr body = block();

    // **Every guard this function made is cleared at its entry.**
    if (guardSlots_.size() > guardsFrom) {
        std::vector<StmtPtr> withClears;
        for (std::size_t i = guardsFrom; i < guardSlots_.size(); i++)
            withClears.push_back(StmtPtr(new ExprStmt(
                setGuard(guardSlots_[i], 0))));
        withClears.push_back(std::move(body));
        body = StmtPtr(new Block(std::move(withClears)));
        guardSlots_.resize(guardsFrom);
    }
    // **[except.spec]/9 over the whole body**, for an exception this function
    // did not throw itself.
    if (inNoexceptFunction_ && mayThrow_ > 0 && !functionHasPads_ &&
        !target_.microsoftNames()) {
        const Type *voidPtr = types_.pointerTo(types_.get(Kind::Void));
        const int pointerSlot = allocateFrameSlot(voidPtr);
        const int selectorSlot = allocateFrameSlot(types_.intType());
        functionHasPads_ = true;

        // A catch-all is the empty type string, the spelling `catch (...)`
        // takes, so the chain is the pad itself with nothing to test.
        std::vector<std::string> types;
        types.push_back(std::string());
        std::vector<int> indices;
        indices.push_back(typeIndexFor(std::string()));

        // `__cxa_begin_catch` first, as clang's `__clang_call_terminate` does:
        // the exception is being handled, and the runtime is told so before
        // the process ends.
        std::vector<ExprPtr> beginArgs;
        ExprPtr held(Var::local(".ex.ptr", pointerSlot));
        held->setType(voidPtr);
        beginArgs.push_back(std::move(held));
        std::vector<StmtPtr> padSteps;
        padSteps.push_back(StmtPtr(new ExprStmt(
            runtimeCall("__cxa_begin_catch", voidPtr, std::move(beginArgs)))));
        padSteps.push_back(StmtPtr(new ExprStmt(
            runtimeCall("abort", types_.get(Kind::Void),
                        std::vector<ExprPtr>()))));
        Block *padBlock = new Block(std::move(padSteps));
        padBlock->setScope(-1);

        std::vector<StmtPtr> guarded;
        guarded.push_back(std::move(body));
        Try *t = new Try(std::move(guarded), StmtPtr(padBlock), pointerSlot,
                         selectorSlot, types);
        t->setTypeIndices(indices);
        body = StmtPtr(t);
    }

    resolveGotos();
    variadicBody_ = false;

    // **The by-value parameters Microsoft makes this function destroy.** A `return`
    // already unwinds everything the function owes, parameters included, so these are
    // appended for the one path that does not go through one: falling off the end.
    if (aliveParams != 0) {
        std::vector<StmtPtr> withParams;
        withParams.push_back(std::move(body));
        emitDestructors(withParams, paramsFrom, d.pos);
        body = StmtPtr(new Block(std::move(withParams)));
        alive_.resize(paramsFrom);
    }

    // Members initialise after the bases and the vptr and before the body -
    // so they are stitched in front of the body here, and the vptr and base
    // blocks below then wrap the result in their own order.
    if (!memberInits.empty()) {
        std::vector<StmtPtr> withInits;
        for (std::size_t i = 0; i < memberInits.size(); i++)
            withInits.push_back(std::move(memberInits[i]));
        withInits.push_back(std::move(body));
        body = StmtPtr(new Block(std::move(withInits)));
    }

    // **And in front of all of it, on Microsoft, the most-derived guard.**
    if (memberOf != nullptr && memberOf->hasVptr() &&
        (d.name == localOf(d.qualifier) ||
         d.name == "~" + localOf(d.qualifier))) {
        std::vector<StmtPtr> withVptr = storeVptrs(d.qualifier, memberOf, thisOffset_);
        withVptr.push_back(std::move(body));
        body = StmtPtr(new Block(std::move(withVptr)));
    }

    // **[class.dtor]/8: a written destructor destroys the class's members
    // too**, after its body and before its bases.
    if (memberOf != nullptr && d.name == "~" + localOf(d.qualifier)) {
        std::vector<StmtPtr> withMembers;
        withMembers.push_back(std::move(body));
        std::vector<StmtPtr> mine =
            memberDestructors(d.qualifier, memberOf, thisOffset_, d.pos);
        for (std::size_t i = 0; i < mine.size(); i++)
            withMembers.push_back(std::move(mine[i]));
        body = StmtPtr(new Block(std::move(withMembers)));
    }

    // **A constructor runs the base's first and a destructor runs it last**, which is
    // the order the standard fixes and clang emits. The base's C2 and D2 are what is
    // called, and on Windows there is one name for each, called directly.
    for (std::size_t bn = 0;
         memberOf != nullptr && bn < memberOf->bases().size() &&
         (d.name == localOf(d.qualifier) ||
          d.name == "~" + localOf(d.qualifier)); bn++) {
        const bool building = d.name == localOf(d.qualifier);
        // Bases are built in the order they were written and destroyed in the reverse.
        // **Both walk the list backwards**, because a constructor's call is prepended
        // to the body and a destructor's is appended; forwards put B before A.
        const std::size_t which = memberOf->bases().size() - 1 - bn;
        const Type *base = memberOf->bases()[which].type;
        const int baseAt = memberOf->bases()[which].offset;
        // **A virtual base is not built here.**
        if (memberOf->bases()[which].isVirtual) continue;
        const std::string key = building ? constructorKey(base->tag())
                                         : destructorKey(base->tag());

        if (const std::vector<std::size_t> *set = overloadsOf(key)) {
            // The initialiser list's arguments for this base, or none - in
            // which case the default constructor is what runs, and a base
            // without one is refused where the reader can fix it.
            std::vector<ExprPtr> chosenArgs;
            std::map<std::string, std::vector<ExprPtr> >::iterator named =
                baseArgs.find(base->tag());
            // **Held by value, like everything else that comes out of
            // overload resolution.** A pointer into `functions_` is a pointer
            // into a vector that anything parsed after this can move.
            Signature chosen;
            bool found = false;
            if (building && named != baseArgs.end()) {
                chosenArgs.swap(named->second);
                chosen = resolveOverload(key, chosenArgs, d.pos);
                found = true;
            } else if (building) {
                // No entry names this base, so its default constructor runs.
                if (const Signature *dc = defaultConstructorOf(base)) {
                    // **Marked used, because a copy is what is kept.**
                    markUsed(dc);
                    chosen = *dc;
                    found = true;
                }
            } else {
                for (std::size_t k = 0; k < set->size(); k++)
                    if (functions_[(*set)[k]].params.empty()) {
                        markUsed(&functions_[(*set)[k]]);
                        chosen = functions_[(*set)[k]];
                        found = true;
                    }
            }
            if (!found)
                src_.fail(d.pos, "'" + base->tag() + "' has no constructor "
                                 "taking nothing - name one in the initialiser "
                                 "list, ': " + base->tag() + "(...)'");
            // **The defaults the entry left out are read here, as every other call reads them.**
            if (building) applyDefaults(chosen, chosenArgs, d.pos);

            std::string symbol = chosen.symbol;
            if (!target_.microsoftNames()) {
                std::string sub;
                if (building) {
                    const Type *fnType = types_.functionType(types_.get(Kind::Void),
                                                             chosen.params,
                                                             false);
                    std::string why;
                    itaniumConstructorName(base->tag(), base, fnType, false,
                                           &sub, &why);
                } else {
                    itaniumDestructorName(base->tag(), base, false, &sub);
                }
                symbol = sub;
            }

            const Type *basePtr = types_.pointerTo(base);
            ExprPtr me(Var::local("this", thisOffset_));
            if (baseAt == 0) {
                me->setType(basePtr);      // the first base is the object
            } else if (memberOf->bases()[which].isVirtual) {
                // **A virtual base is reached by a constant here**, where
                // everywhere else it is reached through the vtable.
                const Type *chars = types_.pointerTo(types_.get(Kind::Char));
                me->setType(types_.pointerTo(memberOf));
                ExprPtr asChars(new Cast(chars, std::move(me)));
                asChars->setType(chars);
                ExprPtr step(new Num(static_cast<long long>(baseAt)));
                step->setType(types_.get(Kind::LongLong));
                ExprPtr moved(new Binary(BinOp::Add, std::move(asChars),
                                         std::move(step)));
                moved->setType(chars);
                me = ExprPtr(new Cast(basePtr, std::move(moved)));
                me->setType(basePtr);
            } else {
                me->setType(types_.pointerTo(memberOf));
                me = convert(std::move(me), basePtr);
            }
            std::vector<ExprPtr> args;
            args.push_back(std::move(me));
            std::vector<const Type *> params2;
            params2.push_back(basePtr);
            // A base with virtual bases takes its sub-VTT out of this one's.
            if (takesVtt(base)) {
                args.push_back(vttForBase(memberOf, base));
                params2.push_back(vttType());
            }
            for (std::size_t i = 0; i < chosen.params.size(); i++) {
                args.push_back(std::move(chosenArgs[i]));
                params2.push_back(chosen.params[i]);
            }
            ExprPtr call = completeCall(base->tag(), symbol, nullptr,
                                        types_.get(Kind::Void), params2, false,
                                        d.pos, std::move(args), false, 0);

            std::vector<StmtPtr> wrapped;
            if (building) {
                wrapped.push_back(StmtPtr(new ExprStmt(std::move(call))));
                wrapped.push_back(std::move(body));
            } else {
                wrapped.push_back(std::move(body));
                wrapped.push_back(StmtPtr(new ExprStmt(std::move(call))));
            }
            body = StmtPtr(new Block(std::move(wrapped)));
        }
    }

    // **The most-derived guard goes outside the base calls**, because a
    // virtual base is built before every non-virtual one: cl's `R::R` stores
    // the vbtable pointer and calls `V::V` and only then `D1::D1`.
    if (msVbInitSlot_ >= 0 && memberOf != nullptr &&
        d.name == localOf(d.qualifier)) {
        std::vector<StmtPtr> guarded =
            guardedVirtualBaseInit(memberOf, d.qualifier, thisOffset_,
                                   msVbInitSlot_, d.pos, -1, false, &baseArgs);
        if (!guarded.empty()) {
            guarded.push_back(std::move(body));
            body = StmtPtr(new Block(std::move(guarded)));
        }
    }

    int frame = alignTo(frameSize_, 16);
    const Type *emittedReturn = d.type->isReference()
                              ? types_.pointerTo(d.type->referent()) : d.type;
    if (definedSymbol.empty())
        definedSymbol = lookupSignature(d.name, params, variadic, d.pos).symbol;

    // Recorded before `body` is moved into the Function - the expression the pointer
    // names is heap-allocated and goes on living there, which makes it safe to keep.
    // Only a definition has one; a declaration with no body folds through nothing.
    if (constexprFunction && body != nullptr) {
        const Expr *value = singleReturnValue(*body);
        if (value == nullptr)
            src_.fail(d.pos, "'" + d.name + "' is 'constexpr', so in C++11 its "
                             "body has to be a single return statement and "
                             "nothing else - that restriction is what lets its "
                             "value be worked out while compiling");
        ConstexprFn fn;
        fn.value = value;
        fn.pos = d.pos;
        for (std::size_t i = 0; i < paramSlots.size(); i++)
            fn.slots.push_back(paramSlots[i].offset);
        constexprFns_[definedSymbol] = fn;
    }
    currentClass_ = nullptr;
    currentFunction_.clear();
    currentFunctionName_.clear();
    localTypes_.clear();
    // **`static` on a member says which member, not which linkage.**
    const bool internal = memberOf != nullptr ? inUnnamedNamespace_
                                              : internalLinkage(sc);
    program.functions.push_back(Function(d.name, emittedReturn, std::move(paramSlots),
                                         std::move(body), frame,
                                         internal, sretSlot,
                                         variadic, regSaveSlot, d.pos,
                                         std::move(fnVars_)));
    program.functions.back().setSymbol(definedSymbol);
    // The definition side of the same question: a member's first parameter is
    // its `this`, and on the Microsoft ABI that is what the hidden return
    // pointer has to come *after*.
    program.functions.back().setHasThis(!d.qualifier.empty() && !inStaticMember_);
    program.functions.back().setHasLandingPads(functionHasPads_);
    program.functions.back().setNoexcept(inNoexceptFunction_);
    // Everything replayed from inside a class body - and every member of a
    // template specialization, which is replayed the same way - is implicitly
    // inline, so its definition may appear in several translation units.
    program.functions.back().setInline((replayingInline_ || inlineFunction) &&
                                        !internal);
    functionHasPads_ = false;
    functionTypes_.clear();
    functionHasTry_ = false;
    // A constructor is emitted under both of Itanium's names: C1 for a
    // complete object, C2 for a base subobject, the second as a label in front
    // of the first. The Microsoft ABI has one name and wants no alias.
    const bool splitForVirtualBase = memberOf != nullptr &&
                                     !target_.microsoftNames() &&
                                     memberOf->hasVirtualBase();
    std::string splitC1, splitC2, splitD1, splitD2;
    if (memberOf != nullptr && d.name == localOf(d.qualifier) &&
        !target_.microsoftNames()) {
        const Type *fnType = types_.functionType(types_.get(Kind::Void), params, false);
        std::string c2, why;
        if (itaniumConstructorName(d.qualifier, findTypedef(d.qualifier),
                                   fnType, false, &c2, &why)) {
            if (splitForVirtualBase) {
                splitC1 = program.functions.back().symbol();
                splitC2 = c2;
                program.functions.back().setSymbol(c2);
            } else {
                program.functions.back().setAlias(c2);
            }
        }
    }
    // **The Microsoft split is the destructor's alone.**
    if (memberOf != nullptr && d.name == "~" + localOf(d.qualifier) &&
        target_.microsoftNames() && memberOf->hasVirtualBase()) {
        splitD1 = vbaseDestructorSymbol(d.qualifier);
        splitD2 = program.functions.back().symbol();
    }
    if (memberOf != nullptr && d.name == "~" + localOf(d.qualifier) &&
        !target_.microsoftNames()) {
        std::string d2;
        itaniumDestructorName(d.qualifier, memberOf, false, &d2);
        if (splitForVirtualBase) {
            splitD1 = program.functions.back().symbol();
            splitD2 = d2;
            program.functions.back().setSymbol(d2);
        } else {
            program.functions.back().setAlias(d2);
        }
    }
    // The deleting form is emitted beside the destructor that was just
    // defined, because that is where its body comes from.
    if (memberOf != nullptr && d.name == "~" + localOf(d.qualifier) &&
        member != nullptr &&
        member->isVirtual)
        synthesizeDeleting(d.qualifier, memberOf, member->access, d.pos);
    program.functions.back().setBlocks(std::move(blocks_));
    // C1 and D1 beside the C2 and D2 just emitted. Last of all, because each
    // pushes a function of its own and every line above wants
    // `functions.back()` to still be the one being defined.
    if (!splitC1.empty())
        synthesizeCompleteCtor(memberOf, params, splitC1, splitC2,
                               program.functions.back().isInline(), d.pos,
                               -1, false, &baseArgs);
    if (!splitD1.empty())
        synthesizeCompleteDtor(memberOf, splitD1, splitD2,
                               program.functions.back().isInline(), d.pos);
}

Program Parser::parse() {
    Program program;
    current_ = &program;
    while (peek().kind != TokenKind::End)
        topLevel(program);
    // **Each of these two can give the other more to do, so they alternate.**
    for (int pass = 0; pass < 64; pass++) {
        const std::size_t had = program.functions.size();
        instantiatePending();
        defineImplicitFunctions();
        if (program.functions.size() == had) break;
    }
    finishDynamicInit(program);
    if (program.functions.empty())
        src_.fail(0, "the file defines no functions");
    return program;
}
