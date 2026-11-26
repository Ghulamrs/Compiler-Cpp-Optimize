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
    // [dcl.link]/4: the innermost specification decides - `extern "C++"` inside a C block.
    const int outer = cLinkage_;
    if (c) cLinkage_++;
    else cLinkage_ = 0;

    if (consume("{")) {
        while (!peek().is("}")) {
            if (peek().kind == TokenKind::End)
                src_.fail(pos, "this 'extern \"" + language + "\"' block is "
                               "never closed");
            topLevel(*current_);
        }
        at_++;
    } else {
        directLinkage_ = true;
        topLevel(*current_);
        directLinkage_ = false;
    }

    cLinkage_ = outer;
    return true;
}

// **A declaration made only for a unit that spells its name** (1.76; this compiler's own attribute, for its own
// headers). `[[__cxx1::when_spelled(wcout)]] static basic_ostream<wchar_t> &wcout = ...;` is read as it is
// written when the identifier `wcout` stands anywhere after the declaration in the translation unit, and is
// passed over whole - its initialiser not read, so nothing in it instantiated - when it does not.
// It is there for <iostream>: [iostream.objects] has eight streams, and a library that is all headers built the
// four wide ones, their buffers and their facets into every program that printed with `std::cout` - a quarter
// of such a program's code on the C6747, where CCS's SampleExt then no longer fitted the 256 KB its own linker
// command file gives it. A library in object files gets this from the linker, which leaves out what nothing
// refers to; a header cannot ask whether a name will be used, and the parser, which holds the unit's tokens
// before it reads the first, can. "Spelled" errs one way only: a name spelled and never used keeps a
// declaration nobody needed, which is what every program had until now.
bool Parser::passedOverUnspelled() {
    if (!(peek().is("[") && peekAt(1).is("[") && peekAt(2).is("__cxx1") && peekAt(3).is("::") &&
          peekAt(4).is("when_spelled") && peekAt(5).is("(") && peekAt(6).kind == TokenKind::Ident &&
          peekAt(7).is(")") && peekAt(8).is("]") && peekAt(9).is("]")))
        return false;
    const std::string name = peekAt(6).text;
    // The declaration ends at its ';' outside every bracket.
    std::size_t end = at_ + 10;
    int depth = 0;
    for (; tokens_[end].kind != TokenKind::End; end++) {
        const Token &t = tokens_[end];
        if (t.is("(") || t.is("[") || t.is("{")) depth++;
        else if (t.is(")") || t.is("]") || t.is("}")) depth--;
        else if (depth == 0 && t.is(";")) break;
    }
    if (tokens_[end].kind == TokenKind::End)
        src_.fail(peek().pos, "the declaration after '[[__cxx1::when_spelled]]' never ends");
    for (std::size_t k = end + 1; tokens_[k].kind != TokenKind::End; k++)
        if (tokens_[k].kind == TokenKind::Ident && tokens_[k].text == name) {
            at_ += 10;                // spelled: the attribute has done its work, and the declaration is read
            return false;
        }
    at_ = end + 1;
    return true;
}

void Parser::topLevel(Program &program) {
    const ReadingAt reading(this, &declAt_);
    if (consume(";")) return;     // [dcl.dcl]: an empty-declaration
    if (passedOverUnspelled()) return;
    noreturnAt_ = 0;
    // `namespace N { ... }` - a scope that qualifies what is declared in it, and nothing else.
    // Everything inside is read by this same function, so a namespace nests, may be reopened,
    // and may hold anything a file may hold. `inline namespace N { }` is [namespace.def]/8.
    const bool inlineNs = peek().is("inline") && peekAt(1).is("namespace");
    if (inlineNs) at_++;
    if (peek().is("namespace")) {
        const std::size_t pos = peek().pos;
        at_++;
        if (inlineNs && peekAt(1).is("="))
            src_.fail(pos, "'inline' goes on a namespace definition, and this is an alias");
        // **An unnamed namespace, opened under the name the ABI gives it.** An `inline` one
        // is that with the mark: its using-directive already makes every member visible.
        if (peek().is("{")) {
            at_++;
            const std::string tag = "_GLOBAL__N_1";
            const bool outer = inUnnamedNamespace_;
            namespaceStack_.push_back(tag);
            namespaces_.insert(namespacePrefix().substr(
                                   0, namespacePrefix().size() - 2));
            if (inlineNs) inlineNamespaces_.insert(namespacePrefix().substr(0, namespacePrefix().size() - 2));
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
        const std::size_t namePos = peek().pos;
        std::string name = expectIdent("a namespace name");
        // `namespace N::M { }` is C++17; nesting is written out here.
        if (peek().is("::"))
            src_.fail(peek().pos, "a nested namespace written 'N::M' is C++17 "
                                  "- open them one at a time");
        if (consume("=")) { namespaceAlias(name, pos); return; }
        expect("{");
        namespaceStack_.push_back(name);
        const std::string path = namespacePrefix().substr(0, namespacePrefix().size() - 2);
        // The first definition decides: reopened later with or without `inline` it stays
        // what it was, and a plain one reopened `inline` is refused as clang refuses it.
        if (inlineNs && namespaces_.count(path) && !inlineNamespaces_.count(path))
            src_.fail(namePos, "non-inline namespace cannot be reopened as inline");
        if (inlineNs) inlineNamespaces_.insert(path);
        namespaces_.insert(path);
        // Nested in an inline one it is the enclosing namespace's too: `lib::inner` for `lib::v2::inner`.
        {
            const std::vector<std::string> alts = inlineAliasPrefixes(namespacePrefix());
            for (std::size_t a = 0; a < alts.size(); a++)
                namespaces_.insert(alts[a].substr(0, alts[a].size() - 2));
        }
        const bool aliased = !inlineAliasPrefixes(namespacePrefix()).empty();
        while (!peek().is("}")) {
            if (peek().kind == TokenKind::End)
                src_.fail(pos, "this namespace never closes");
            const TableSizes before = tableSizes();
            topLevel(program);
            if (aliased) aliasInlineMembers(before);
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
        if (aliasDeclaration(namespacePrefix())) return;
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
            const std::string written = target;
            // [basic.lookup.qual]/1: the qualifier's first name is found innermost first, so
            // `using s::Sy;` inside `namespace a` names `a::s::Sy`.
            if (!fromGlobal && cut != std::string::npos) {
                const std::string::size_type first = target.find("::");
                const std::string scope = namespaceNamed(target.substr(0, first));
                if (!scope.empty()) target = scope + target.substr(first);
            }
            // What another using-declaration put there: `using std::scanf;` over <cstdio>'s `using ::scanf;`.
            target = followUsingDeclaration(target);
            // **Refused by name if it names nothing**, at the declaration
            // that says which name was meant; a class template is named as
            // anything else is, the template it was ([namespace.udecl]).
            const std::map<std::string, TemplateDecl>::const_iterator tmplNamed = findTemplate(target);
            if (tmplNamed != templates_.end() && tmplNamed->second.isClass && cut != std::string::npos &&
                tmplNamed->second.ns == target.substr(0, target.rfind("::") + 2))
                templateUsing_[namespacePrefix() + target.substr(target.rfind("::") + 2)] = tmplNamed->first;
            else if (!hasTypeNamed(target) && !hasFunctionNamed(target) &&
                !hasGlobalNamed(target) &&
                namespaces_.find(target) == namespaces_.end())
                src_.fail(pos, "'" + written + "' is not declared, so there is "
                               "nothing here for this using-declaration to "
                               "name");
            const std::string shortName =
                cut == std::string::npos ? written : written.substr(cut + 2);
            const std::string declared = namespacePrefix() + shortName;
            // **[namespace.udecl]/10: a using-declaration of a function adds its overloads to the set the name
            // has here** - `using N::f; using M::f;` is one set, and a function of the name declared here
            // already is in it (R11; an alias to one of them hid the rest). What N adds later stays out (/11).
            if (declared != target && hasFunctionNamed(target) && !hasTypeNamed(target) && !hasGlobalNamed(target)) {
                std::vector<std::size_t> from = *overloadsOf(target);
                auto had = usingDeclarations_.find(declared);
                if (had != usingDeclarations_.end()) {
                    if (const std::vector<std::size_t> *earlier = overloadsOf(had->second))
                        from.insert(from.begin(), earlier->begin(), earlier->end());
                    usingDeclarations_.erase(had);
                }
                std::vector<std::size_t> &set = functionIndex_[declared];
                for (std::size_t k = 0; k < from.size(); k++)
                    if (std::find(set.begin(), set.end(), from[k]) == set.end()) set.push_back(from[k]);
                return;
            }
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
        // The directive names the namespace an alias stands for, [namespace.alias]/2.
        const std::string resolved = resolveNamespaceName(opened);
        usingNamespaces_.push_back(resolved.empty() ? opened : resolved);
        return;
    }

    // Taken before anything nested can read it: `extern "C" extern "C" int x;` sets it again.
    const bool direct = directLinkage_;
    directLinkage_ = false;
    if (linkageSpecification()) return;
    if (externTemplateDeclaration()) return;
    if (templateDeclaration()) return;

    StorageClass sc;
    Qualifiers quals;
    std::size_t scPos = peek().pos;
    const std::size_t specStart = at_;
    const Type *base = specifiers(&sc, &quals);
    // [dcl.link]/7: linkage, and whether this defines, are what `extern` would give - so
    // `extern "C" int u;` declares and `extern "C" const int k = 5;` is external. No storage class.
    if (direct) {
        if (sc == StorageStatic || sc == StorageExtern)
            src_.fail(scPos, std::string("a declaration written straight after 'extern \"C\"' takes no storage "
                             "class - [dcl.link]/7 already makes it '") + (sc == StorageStatic ? "extern', and 'static' contradicts it"
                             : "extern', and saying it twice is refused"));
        if (sc == StorageNone) sc = StorageExtern;
    }

    if (peek().is(";")) {
        // [class.union]/6: a nameless union declares its members here; a nameless struct is clang's refusal.
        if (base->isStructOrUnion() && base->tag().empty()) {
            std::size_t key = specStart;
            while (key + 1 < at_ && !tokens_[key].is("union") && !tokens_[key].is("struct") && !tokens_[key].is("class")) key++;
            if (base->kind() != Kind::Union)
                src_.fail(tokens_[key].pos, "anonymous structs are a GNU extension");
            declareAnonymousUnionObject(base, sc, tokens_[key].pos, false);
        }
        at_++;
        return;
    }

    if (sc == StorageRegister)
        src_.fail(scPos, "'register' is a storage class for a local or a "
                         "parameter, and this is file scope");
    if (sc == StorageAuto)
        src_.fail(scPos, "'auto' is a storage class for a local, and this is "
                         "file scope - every object here has static duration");

    if (sc == StorageTypedef) {
        const bool definesClass = typedefDefinesClass(specStart, at_);
        do {
            Declared td = declarator(base);
            refuseThreadLocalHere(quals.isThreadLocal || quals.threadLocalAsStatic, td.pos);
            typedefFunctionSuffix(td);
            // **A typedef at namespace scope is keyed by its qualified name**, the way a class's tag is.
            const std::string key = namespacePrefix() + td.name;
            // [dcl.typedef]/2 lets this scope's own be redeclared to the same type ("typedef struct S S;"),
            // and a namespace's `typedef short T;` hides a global `T` rather than redeclaring it.
            if (const Type *had = findGlobalTypedef(key))
                if (had != td.type && typedefs_[typedefIndex_.find(key)->second].name == key)
                    src_.fail(td.pos, "'" + td.name + "' is typedefed twice, "
                                      "and not to the same type: it was '" +
                                      had->describe() + "' and is now '" +
                                      td.type->describe() + "'");
            typedefIndex_[key] = typedefs_.size();
            typedefs_.push_back(TypedefName{ key, td.type });
            if (definesClass && td.type->tag().empty()) td.type->nameForLinkage(key, td.name, nullptr, key != td.name);
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
    freeSlots_.clear();
    Declared d = declarator(base);
    if (fnPointerSpec_ == 1) noexceptPointers_.insert(d.name);
    // A closure's `operator()` reads the enclosing function's const locals for their values,
    // [expr.prim.lambda]/12 - declared first, so that a parameter or a local of the body hides one (1.27).
    {
        std::map<std::string, std::vector<Local> >::const_iterator cc = closureConstants_.find(d.qualifier);
        if (cc != closureConstants_.end()) locals_ = cc->second;
    }

    // **A `constexpr` function was refused by name until 7.5b**: accepting it as an
    // ordinary function would compile and then quietly fail to be constant where the
    // keyword was written for. The '(' still ahead tells a function from an object.
    // An operator is always a function: `constexpr Q operator+(Q b) const` in a class template's replay, where
    // `(Q b)` was taken for an initialiser and the return type kept its const (1.82).
    const bool constexprFunction =
        quals.isConstexpr &&
        ((peek().is("(") && (!atParenInitialiser() || d.name.compare(0, 8, "operator") == 0)) || d.paramsAt != 0 ||
         d.type->isFunction());

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
    // [dcl.constexpr]/2: a constexpr function is implicitly inline. 1.81: it was an ordinary definition, and a
    // header's constexpr function included by two files was "already defined" at the link.
    const bool inlineDefinition = inlineFunction || constexprFunction;

    // **`constexpr` does not make the return type const**, and it is measured rather
    // than reasoned: cl and clang both spell `constexpr int sq(int)` as ?sq@@YAHH@Z.
    // The keyword sets isConst because on an *object* that is exactly what it means.
    if (constexprFunction && !d.type->isFunction())
        d.type = types_.withoutConst(d.type);
    // And where the declarator has read the parameters already - `constexpr Q operator+(Q b) const`, an operator,
    // in a class template's replay - from the function type's return (1.82: "declared to return 'struct Q<1>' and
    // this says 'const struct Q<1>'").
    else if (constexprFunction && d.type->isFunction() && d.type->returns() != nullptr && d.type->returns()->isConst())
        d.type = types_.functionType(types_.withoutConst(d.type->returns()), d.type->params(), d.type->isVariadicFn(),
                                     d.type->refQualifier(), d.type->cvThis());

    // [dcl.align]/1: an alignment-specifier appertains to a variable, a data member or a class.
    if (quals.alignAs != 0 && (peek().is("(") || d.paramsAt != 0 || d.type->isFunction()))
        src_.fail(d.pos, "'alignas' cannot be applied to a function - [dcl.align]/1");
    // [basic.start.main]/2-3: the global `main` returns int and is neither static, inline nor constexpr.
    if (d.name == "main" && d.qualifier.empty() && namespaceStack_.empty() && classStack_.empty() &&
        cLinkage_ == 0 && (peek().is("(") || d.paramsAt != 0)) {
        if (d.type->unqualified()->kind() != Kind::Int || d.type->isConst() || d.type->isVolatile())
            src_.fail(d.pos, "'main' must return 'int' - [basic.start.main]/2");
        if (sc == StorageStatic || inlineFunction || constexprFunction)
            src_.fail(d.pos, std::string("'main' is not allowed to be declared ") +
                             (sc == StorageStatic ? "static" : inlineFunction ? "inline" : "constexpr") +
                             " - [basic.start.main]/3");
    }

    // **`int ns::f(int)` defines the `f` declared in namespace `ns`**: a
    // qualifier that names a namespace is folded into the name, which is how
    // a function in a namespace is keyed throughout (the review's A17).
    std::vector<std::string> definedIn;
    std::string absoluteName;
    if (!d.qualifier.empty() && namespaces_.find(d.qualifier) != namespaces_.end() &&
        findTypedef(d.qualifier) == nullptr) {
        // [basic.lookup.unqual]/6: its body looks names up in that namespace, as if written there.
        for (std::size_t s = 0, e; s <= d.qualifier.size(); s = e + 2) {
            e = d.qualifier.find("::", s);
            if (e == std::string::npos) e = d.qualifier.size();
            definedIn.push_back(d.qualifier.substr(s, e - s));
        }
        // Relative to the namespace it is written in, which every key below puts back on;
        // lookupSignature takes a qualified name as absolute, so it is handed the whole one.
        absoluteName = d.qualifier + "::" + d.name;
        const std::string prefix = namespacePrefix();
        const bool within = !prefix.empty() && d.qualifier.compare(0, prefix.size(), prefix) == 0;
        d.name = within ? absoluteName.substr(prefix.size()) : absoluteName;
        d.qualifier.clear();
    } else if (!d.qualifier.empty()) {
        // A member of a class in a namespace: its body, parameters and initialiser read there too.
        const Type *owner = findTypedef(d.qualifier);
        if (owner != nullptr && owner->isStructOrUnion()) definedIn = classNamespacePath(owner);
    }
    struct DefinitionNamespace {
        std::vector<std::string> &slot; std::vector<std::string> saved;
        ~DefinitionNamespace() { slot = saved; }
    } definitionScope{ definitionNamespace_, definitionNamespace_ };
    if (!definedIn.empty()) definitionNamespace_ = definedIn;
    struct NamespaceSwap {
        std::vector<std::string> &stack; std::vector<std::string> saved; bool on;
        void enter(const std::vector<std::string> &to) { if (to.empty()) return; saved = stack; stack = to; on = true; }
        void leave() { if (on) stack = saved; on = false; }
        ~NamespaceSwap() { leave(); }
    } inNamespace{ namespaceStack_, {}, false };

    // **`S g(1);` at file scope is a construction, not a prototype**: a
    // parameter list begins with a type name or is empty, the local path's own
    // question, and `S g();` is the function C++ says it is.
    bool scalarInitAhead = false;
    if (peek().is("(") && d.paramsAt == 0 && d.qualifier.empty() &&
        !d.type->isStructOrUnion() && !d.type->isArray() &&
        !d.type->isReference() && !d.type->isFunction() &&
        (sc != StorageExtern || direct) && !constexprFunction && !inlineFunction) {
        inNamespace.enter(definedIn);
        scalarInitAhead = atParenInitialiser();
        inNamespace.leave();
    }

    // And `int &g(x);`: a reference bound by a parenthesised initialiser (1.79: read as a function's
    // declaration; a reference to function, `void (&f)(int)`, has its parameters in the declarator).
    bool referenceInitAhead = false;
    if (peek().is("(") && d.paramsAt == 0 && d.qualifier.empty() && d.type->isReference() &&
        !d.type->referent()->isFunction() && (sc != StorageExtern || direct) && !constexprFunction &&
        !inlineFunction) {
        inNamespace.enter(definedIn);
        referenceInitAhead = atParenInitialiser();
        inNamespace.leave();
    }

    bool constructionAhead = scalarInitAhead || referenceInitAhead;
    if (peek().is("(") && d.paramsAt == 0 && d.type->isStructOrUnion() &&
        !d.type->tag().empty() &&
        hasConstructors(d.type->tag())) {
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
        inNamespace.enter(definedIn);
        defineStaticMember(d, program, quals.isThreadLocal);
        // [dcl.dcl]/3: `int A::x = 0, A::y = 1;` - every declarator of the list defines its own member.
        while (consume(",")) {
            Declared next = declarator(base);
            if (next.qualifier.empty() || next.paramsAt != 0 || next.type->isFunction())
                src_.fail(next.pos, "a declaration that defines a static member defines only static "
                                    "members after it here - write '" + next.name + "' in a declaration of its own");
            defineStaticMember(next, program, quals.isThreadLocal);
        }
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
        refuseNoreturnObject(d.name);
        if (sc != StorageExtern) refuseInaccessibleDestructor(d.type, d.pos, "'" + d.name + "'");
        // `int a::b::v = k;` is read as if written in a::b, its initialiser too - [basic.lookup.unqual]/13.
        if (!absoluteName.empty()) {
            inNamespace.enter(definedIn);
            d.name = absoluteName.substr(absoluteName.rfind("::") + 2);
        }
        const Type *deducedSoFar = nullptr;
        for (;;) {
            d.type = constexprObjectType(d.type, quals);
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
                const std::string symbol = prev != nullptr
                    ? prev->symbol : dataSymbol(gname, d.type, internal, d.pos);
                const bool defines = !(sc == StorageExtern && !peek().is("="));
                std::vector<GlobalPiece> pieces;
                bool hasInit = false;
                const ThreadLocalObject tlo(this, quals.isThreadLocal);
                if (defines) {
                    if (prev != nullptr && prev->emitted)
                        src_.fail(d.pos, "'" + d.name + "' is defined twice");
                    const FunctionState outer = enterInitFunction();
                    bindStaticReference(d, symbol, pieces, hasInit, &initSink());
                    leaveInitFunction(outer);
                }
                if (prev != nullptr) {
                    if (defines) { prev->emitted = true; prev->hasInit = true; }
                } else {
                    globalIndex_[gname] = globals_.size();
                    globals_.push_back(GlobalSym{ gname, symbol, d.type, false,
                                                  defines, defines, false, 0 });
                }
                // A thread_local reference bound in __tls_init is reached through the wrapper, one
                // bound in the image directly; what another unit defines, through the wrapper.
                noteThreadLocal(prev != nullptr ? *prev : globals_.back(), prev != nullptr, quals.isThreadLocal,
                                d.name, defines ? !hasInit : true, !internal, defines, d.pos);
                if (defines)
                    program.globals.push_back(Global{ gname, symbol,
                                                      types_.pointerTo(d.type->referent()),
                                                      std::move(pieces), hasInit,
                                                      internal, false });
                if (defines) program.globals.back().isThreadLocal = quals.isThreadLocal;
                if (!consume(",")) break;
                inNamespace.leave();   // a second declarator is read where the first was written
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
                    (hasConstructors(plain->tag()) ||
                     destructorOf(plain) != nullptr))
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
                // **Built before main**, [basic.start.init]/2, in the init function and in declaration
                // order; destroyed at exit in reverse. `extern S s;` alone builds nothing, and an aggregate
                // from a list is filled member by member below, its members' constructors run there.
                const Type *each = arrayClass != nullptr ? arrayClass : d.type;
                const bool aggregate = isAggregateClass(each) && destructorOf(each) == nullptr &&
                                       bracedInitialiserAhead();
                // A class with no constructor and a destructor is built by nothing, and is still destroyed at
                // exit ([basic.start.term]), so without an initialiser it takes this path for the registration alone.
                const bool destroyedOnly = arrayClass == nullptr && d.type->isStructOrUnion() &&
                                           !d.type->tag().empty() && !hasConstructors(d.type->tag()) &&
                                           destructorOf(d.type->unqualified()) != nullptr &&
                                           !peek().is("=") && !peek().is("(") && !peek().is("{");
                if ((arrayClass != nullptr ||
                     hasConstructors(d.type->tag()) || destroyedOnly) &&
                    sc != StorageExtern && !aggregate) {
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
                    // In scope in its own initialiser - `S s(&s);` - [basic.scope.pdecl]/1.
                    const bool early = prev == nullptr && initialiserNames(d.name);
                    if (early) {
                        globalIndex_[gname] = globals_.size();
                        globals_.push_back(GlobalSym{ gname, symbol, d.type,
                                                      d.type->isConst(), true,
                                                      true, false, 0 });
                        noteThreadLocal(globals_.back(), false, quals.isThreadLocal, d.name, true, !internal, true, d.pos);
                    }
                    const ThreadLocalObject tlo(this, quals.isThreadLocal);
                    if (quals.isThreadLocal) tlsDefining(symbol, !internal);
                    std::vector<GlobalPiece> folded;
                    dynamicInitialise(d, symbol, helper, false, quals.isConstexpr ? &folded : nullptr);
                    if (quals.isConstexpr) constObjects_[gname] = constObjects_[symbol];
                    if (prev != nullptr) {
                        prev->emitted = true;
                        prev->hasInit = true;
                    } else if (!early) {
                        globalIndex_[gname] = globals_.size();
                        globals_.push_back(GlobalSym{ gname, symbol, d.type,
                                                      d.type->isConst(), true,
                                                      true, false, 0 });
                    }
                    if (!early)
                        noteThreadLocal(prev != nullptr ? *prev : globals_.back(), prev != nullptr, quals.isThreadLocal,
                                        d.name, true, !internal, true, d.pos);
                    // Not isConst: the constructor writes it, so it cannot
                    // be laid down read-only - unless it was folded, and no constructor runs.
                    program.globals.push_back(Global{ gname, symbol, d.type, folded,
                                                      quals.isConstexpr, internal, quals.isConstexpr });
                    program.globals.back().align = quals.alignAs;
                    program.globals.back().isThreadLocal = quals.isThreadLocal;
                    if (!consume(",")) break;
                    inNamespace.leave();   // a second declarator is read where the first was written
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
            // **[basic.scope.pdecl]/1: the name is declared before its initialiser** -
            // `void *p = &p;` - so a first declaration naming itself registers here.
            const std::string selfKey = (namespaceStack_.empty() || cLinkage_ > 0)
                                      ? d.name : namespacePrefix() + d.name;
            bool registered = false;
            std::size_t early = 0;
            if (d.type->isComplete() && findGlobalToUpdate(selfKey) == nullptr &&
                initialiserNames(d.name)) {
                const bool c = d.type->isConst();
                const bool hidden = sc == StorageStatic || (c && sc != StorageExtern);
                early = globals_.size();
                registered = true;
                globalIndex_[selfKey] = early;
                globals_.push_back(GlobalSym{ selfKey, dataSymbol(selfKey, d.type, hidden, d.pos),
                                              d.type, c, false, false, false, 0 });
            }
            if (scalarInitAhead || consume("=") || atBracedInitialiser(d.name)) {
                OverloadTarget hold(*this, d.type);
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
                // An aggregate's list, folded leaf by leaf for a constant expression to read.
                if (quals.isConstexpr && d.type->innermostElement()->unqualified()->isStructOrUnion()) {
                    ConstObject value;
                    if (d.type->isArray() || !constObjectFromInit(d.type, in, 0, &value))
                        refuseConstexprClassObject(d.name, d.pos);
                    constObjects_[selfKey] = value;
                }
                // [dcl.constexpr]/9: an array of numbers or pointers, its elements read as a constant object's leaves are.
                bool constantArray = false;
                if (quals.isConstexpr && d.type->isArray() &&
                    (d.type->innermostElement()->isArithmetic() || d.type->innermostElement()->isPointer())) {
                    ConstObject value;
                    constantArray = constObjectFromInit(d.type, in, 0, &value);
                    if (constantArray) constObjects_[selfKey] = value;
                }
                if (quals.isConstexpr && d.type->isPointer()) constantArray = constantPointerInit(in, selfKey);
                if (!constantKnown && !constantDoubleKnown && !constantArray && quals.isConstexpr &&
                    !d.type->innermostElement()->unqualified()->isStructOrUnion())
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
                    dynamicScalar = std::move(in);   // a list too, where a member has a constructor
                }
            } else if (quals.isConstexpr && sc != StorageExtern) {
                src_.fail(d.pos, "'" + d.name + "' is 'constexpr' and has no "
                                 "initialiser - there is nothing for it to be");
            } else if (d.type->isConst() && sc != StorageExtern) {
                // [dcl.init]/7, as locals; not `extern`: defined elsewhere
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
            GlobalSym *prev = registered ? nullptr : findGlobalToUpdate(gname);
            // [dcl.meaning]/1: `int N::x = 1;` defines an x N declared; [basic.scope.declarative]/4: a variable
            // does not take the name of a function in the same scope.
            if (prev == nullptr && !registered && !absoluteName.empty())
                src_.fail(d.pos, "'" + absoluteName + "' is defined with a qualified name, and it was never "
                                 "declared in its namespace - [dcl.meaning]/1");
            if (prev == nullptr && hasFunctionNamed(gname) && cLinkage_ == 0)
                src_.fail(d.pos, "'" + d.name + "' redeclares a function as a variable - "
                                 "[basic.scope.declarative]/4");
            // [dcl.stc]/7: an object declared with external linkage is not redeclared `static`.
            if (prev != nullptr && sc == StorageStatic && internalGlobals_.count(gname) == 0)
                src_.fail(d.pos, "'" + d.name + "' was declared with external linkage, and a later 'static' "
                                 "declaration cannot give it internal linkage - [dcl.stc]/7");
            if (prev != nullptr) {
                const Type *both = composite(prev->type, d.type);
                if (both == nullptr)
                    src_.fail(d.pos, "'" + d.name + "' was already declared as '" +
                                     prev->type->describe() + "', not '" +
                                     d.type->describe() + "'");

                prev->type = both;
                d.type = both;
                // [basic.def.odr]/1: `int v;` already defines - C++ has no tentative definition.
                const bool defines = sc != StorageExtern || hasInit || (dynamicScalar.value != nullptr || dynamicScalar.isList);
                if (defines && prev->emitted)
                    src_.fail(d.pos, "'" + d.name + "' is defined twice");
                if ((hasInit || (dynamicScalar.value != nullptr || dynamicScalar.isList)) && prev->hasInit)
                    src_.fail(d.pos, "'" + d.name + "' is given an initialiser twice");
                if (hasInit || (dynamicScalar.value != nullptr || dynamicScalar.isList)) prev->hasInit = true;
                const ThreadLocalObject tlo(this, quals.isThreadLocal);
                if ((dynamicScalar.value != nullptr || dynamicScalar.isList))
                    dynamicInitialiseScalar(gname, d.type, dynamicScalar);
                noteThreadLocal(*prev, true, quals.isThreadLocal, d.name, (dynamicScalar.value != nullptr || dynamicScalar.isList) || !defines,
                                !internalLinkage(sc), defines, d.pos);

                // `extern` with an initialiser defines - [dcl.stc]/6 - and
                // keeps external linkage a const object would otherwise lose.
                if (sc != StorageExtern || hasInit || (dynamicScalar.value != nullptr || dynamicScalar.isList)) {
                    if (!prev->emitted) {
                        prev->emitted = true;
                        program.globals.push_back(Global{ d.name, prev->symbol,
                                                          d.type, pieces, hasInit,
                                                          internalLinkage(sc),
                                                          prev->isConst });
                        program.globals.back().isThreadLocal = quals.isThreadLocal;
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
                inNamespace.leave();   // a second declarator is read where the first was written
                d = declarator(base);
                continue;
            }

            // A variable declared in a namespace is keyed and mangled by its qualified
            // name, the same as a function. `extern "C"` does not reach into one, so a
            // name with C linkage keeps what it was written with - `gname` above.
            globalIndex_[gname] = registered ? early : globals_.size();
            bool objectIsConst = d.type->isConst();
            // A const object at namespace scope has internal linkage of its own -
            // [basic.link]/3 - which is why a header may define one and C, where it
            // would be external, may not. Nothing outside can name it.
            bool internal = sc == StorageStatic ||
                            (objectIsConst && sc != StorageExtern);
            if (internal) internalGlobals_.insert(gname);
            std::string symbol = dataSymbol(gname, d.type, internal, d.pos);
            const bool dynamic = (dynamicScalar.value != nullptr || dynamicScalar.isList);
            GlobalSym made{ gname, symbol, d.type, objectIsConst,
                            sc != StorageExtern || hasInit || dynamic,
                            hasInit || dynamic, constantKnown, constantValue };
            made.isConstantDouble = constantDoubleKnown;
            made.constantDouble = constantDoubleValue;
            if (registered) globals_[early] = made;
            else globals_.push_back(made);
            const bool definesHere = sc != StorageExtern || hasInit || dynamic;
            noteThreadLocal(registered ? globals_[early] : globals_.back(), false, quals.isThreadLocal, d.name,
                            dynamic || !definesHere, !internal, definesHere, d.pos);
            const ThreadLocalObject tlo(this, quals.isThreadLocal);
            if (dynamic) dynamicInitialiseScalar(gname, d.type, dynamicScalar);
            if (definesHere) {
                program.globals.push_back(Global{ gname, symbol, d.type,
                                                  std::move(pieces), hasInit,
                                                  internal, objectIsConst && !dynamic });
                program.globals.back().align = quals.alignAs;
                program.globals.back().isThreadLocal = quals.isThreadLocal;
                refuseWeakAlignas(quals.alignAs, d.type, d.pos);
            }
            if (!consume(",")) break;
            inNamespace.leave();   // a second declarator is read where the first was written
                d = declarator(base);
        }
        expect(";");
        return;
    }

    refuseThreadLocalHere(quals.isThreadLocal || quals.threadLocalAsStatic, d.pos);
    // `auto f(int a) -> decltype(a)`, C++11: the return type is read after the parameters,
    // which are in scope for it by then, [dcl.fct]/2 and [basic.scope.param].
    const bool trailingArrow = trailingArrowAhead();
    const bool trailing = trailingArrow && mentionsDeduced(d.type);
    if (trailing) refuseTrailingBase(d.type, d.pos);
    if (mentionsDeduced(d.type) && !trailing)
        src_.fail(d.pos, "a function's return type cannot be deduced - `auto` "
                         "there is C++14, and this compiler is C++11");

    std::size_t resumeAt = 0;
    if (d.paramsAt != 0) {
        resumeAt = at_;
        at_ = d.paramsAt;
    }

    inNamespace.enter(definedIn);   // the parameters, as the body
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
    std::vector<std::string> paramNames;
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
                            const Type *pt = packTypes[k];   // a reference member arrives as its address
                            paramSlots.push_back(Param{ pt->isReference() ? types_.pointerTo(pt->referent()) : pt, poff });
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
                Declared pd = parameterDeclarator(pt);
                if (peek().is("(")) pd.type = functionTypeParameter(pd.type);
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
                paramNames.push_back(pd.name);
                paramSlots.push_back(Param{ held->isReference()
                                            ? types_.pointerTo(held->referent())
                                            : held, off });
                // A default written on the *definition*. The parameter list here is
                // read by this loop and not by parameterTypes, so the same recording
                // happens twice, and the same way: a place in the token stream.
                defaults.resize(params.size(), 0);
                if (consume("=")) {
                    defaults.back() = at_;
                    skipDefaultArgument();
                    checkDefaultArgument(defaults.back(), at_, paramNames, nullptr, false);
                }
                if (consume(")")) break;
                expect(",");
            }
        }
    }
    inNamespace.leave();
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
    // Checked against the earlier declarations' defaults where they meet ([dcl.fct.default]/4, R8).
    // **A list with no default hands on none** (1.65): pendingDefaults_ was left as it stood, and what stood
    // there was the last parameter list parameterTypes read - one of std::string's members, say, instantiated
    // in the function before. declare() merged that into this function's own defaults: `void g(int depth = 0);`
    // then `void h() { std::string t; }` then `void g(int depth) { }` was refused as a default followed by a
    // parameter without one (cppparse's PathUtil.cpp, which 1.30 compiled).
    if (sawDefault) {
        pendingDefaultsAt_ = d.pos;
        pendingDefaults_ = defaults;
    } else {
        pendingDefaults_.clear();
    }


    if (peek().is("(") || (peek().is("[") && !peekAt(1).is("["))) {
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
    bool constThis = false, volatileThis = false;
    while (memberOf != nullptr && (peek().is("const") || peek().is("volatile"))) {
        if (consume("const")) constThis = true;
        else if (consume("volatile")) volatileThis = sawVolatile();
    }
    // The same C++11 rule the class body applies, to every member function but a constructor - and
    // a static one, which has no `this` to be const: the lookup below lets that one match without.
    const bool impliedConst = memberOf != nullptr && constexprFunction && !constThis &&
                              d.name != localOf(d.qualifier);
    if (impliedConst) constThis = true;
    // [dcl.fct]/6: a ref-qualifier is a non-static member's, so `int f() &` at namespace scope is ill-formed.
    const std::size_t refAt = peek().pos;
    const RefQual refThis = refQualifier();
    if (refThis != RefQual::None && memberOf == nullptr)
        src_.fail(refAt, "non-member function '" + d.name + "' cannot have a ref-qualifier - "
                         "[dcl.fct]/6 allows one on a non-static member function alone");

    // The exception specification comes after the constness, which is the order C++ writes them in.
    pendingNoexcept_ = exceptionSpecification();
    skipAttributes();  // [dcl.fct]/1: the function type's own, after the exception specification
    // **Captured here because declareFunction consumes it.**
    const bool declaredNoexcept = pendingNoexcept_;
    const DynamicSpec declaredSpec = pendingSpec_;
    // [except.spec]/3: a destructor's definition says what its declaration said.
    if (memberOf != nullptr && !d.name.empty() && d.name[0] == '~' &&
        (declaredNoexcept || declaredSpec.written || declaredSpec.allowsAll) &&
        destructorMayThrow(memberOf) == declaredNoexcept)
        src_.fail(d.pos, "'" + d.name + "' is defined with an exception specification its declaration does "
                         "not have - [except.spec]/3, every declaration of a function has the same specification");
    if (trailing) d.type = readTrailingReturn(0);

    // A member template only declared, made for the arguments it is asked for: its signature below, no body.
    const bool onlyDeclared = memberOf != nullptr && memberTemplateInst_ && d.name == memberTemplateOf_;
    // `S::S() = default;` is a definition, read below with the member it defines.
    const bool defaultedHere = memberOf != nullptr && pendingSpecial_ == SpecialDef::Defaulted;
    if (!onlyDeclared && !defaultedHere && consume(";")) {
        if (memberOf != nullptr)
            src_.fail(d.pos, "'" + d.qualifier + "::" + d.name + "' is declared "
                             "inside the class - this says it again outside, "
                             "which declares nothing new");
        definingQualified_ = !absoluteName.empty();
        declareFunction(d.name, d.type, params, variadic, false, d.pos,
                        sc == StorageStatic);
        definingQualified_ = false;
        // A prototype's by-value parameters were registered alive above, and
        // there is no body to destroy them: pop them, or the next definition does.
        alive_.resize(alive_.size() - aliveParams);
        return;
    }
    const Signature *member = nullptr;
    // Read before the body: compiling it makes more functions, and `member` points into their table.
    bool memberStatic = false;
    if (memberOf != nullptr) {
        std::string key = d.qualifier + "::" + d.name;   // "Point::~Point" too
        // **A member function template specialization declares itself here.**
        if (memberTemplateInst_ && d.name == memberTemplateOf_) {
            key = d.qualifier + "::" + memberTemplateName_;
            // Declared unless one with these parameters is: two templates of a name meet at K = 3.
            bool declared = false;
            if (const std::vector<std::size_t> *set = overloadsOf(key))
                for (std::size_t k = 0; k < set->size(); k++)
                    if (functions_[(*set)[k]].constThis == constThis &&
                        functions_[(*set)[k]].volatileThis == volatileThis &&
                        functions_[(*set)[k]].refThis == refThis &&
                        sameParameters(functions_[(*set)[k]].params, params))
                        declared = true;
            if (!declared) {
                const Type *fnType =
                    types_.functionType(d.type, params, variadic);
                // Itanium spells the pattern - `RK1VIXT_EE`, the parameter as T_ - and Microsoft
                // the substituted signature, the split a free function template already makes.
                const Type *patternFn = memberTemplatePattern_ != nullptr ? memberTemplatePattern_
                                                                           : fnType;
                // `template <class T> static T f(T)`: no `this`, and cl spells it S, K or C.
                const bool staticTemplate = sc == StorageStatic;
                const char code = memberTemplateAccess_ == Access::Public ? (staticTemplate ? 'S' : 'Q')
                                : memberTemplateAccess_ == Access::Protected ? (staticTemplate ? 'K' : 'I')
                                                                             : (staticTemplate ? 'C' : 'A');
                const bool ctor = d.name == localOf(d.qualifier);
                std::string sym, why, c2;
                bool ok;
                if (ctor && target_.microsoftNames())
                    ok = microsoftConstructorTemplateName(d.qualifier, memberOf, manglingType(fnType),
                                                          memberTemplateArgs_, code, &sym, &why);
                else if (ctor)
                    ok = itaniumConstructorTemplateName(d.qualifier, memberOf, patternFn,
                                                        memberTemplateArgs_, true, &sym, &why) &&
                         itaniumConstructorTemplateName(d.qualifier, memberOf, patternFn,
                                                        memberTemplateArgs_, false, &c2, &why);
                else if (target_.microsoftNames())
                    ok = microsoftMemberTemplateName(d.qualifier, memberOf, d.name,
                          fnType, memberTemplateArgs_, code,
                          (constThis ? Type::kThisConst : 0u) | (volatileThis ? Type::kThisVolatile : 0u),
                          refThis, &sym, &why);
                else
                    ok = itaniumMemberTemplateName(d.qualifier, memberOf, d.name,
                          patternFn, memberTemplateArgs_,
                          (constThis ? Type::kThisConst : 0u) | (volatileThis ? Type::kThisVolatile : 0u),
                          refThis, &sym, &why);
                if (!ok)
                    src_.fail(d.pos, "'" + key + "' cannot be given a name the "
                                     "linker can hold: " + why);
                if (!c2.empty()) constructorC2_[sym] = c2;
                // **Under two keys, as a free specialization is.**
                functionIndex_[d.qualifier + "::" + d.name]
                    .push_back(functions_.size());
                // One key when they meet: `Box<M>`'s constructor template at U = M is `Box<M>::Box<M>`.
                if (key != d.qualifier + "::" + d.name) functionIndex_[key].push_back(functions_.size());
                functions_.push_back(Signature{
                    memberTemplateName_, sym, d.type, params, variadic, false,
                    d.pos, false, d.qualifier, constThis,
                    memberTemplateAccess_, false });
                functions_.back().fromTemplate = true;
                // [temp.func.order]: two member templates' specializations are ordered by their patterns,
                // as free ones are - `(const tuple &)` before `(const tuple<U, V...> &)`.
                functions_.back().pattern = memberTemplatePattern_;
                functions_.back().isStaticMember = staticTemplate;
                functions_.back().refThis = refThis;
                functions_.back().volatileThis = volatileThis;
                // Its exception-specification, read above with the template's arguments bound (1.77: a member
                // template's specialization had none, so `noexcept(shared_ptr<B>(d))` said false and every
                // noexcept function calling one carried a handler for std::terminate).
                functions_.back().isNoexcept = declaredNoexcept;
                functions_.back().spec = declaredSpec;
                // **The defaults its parameter list just read.**
                requireDefaultsAreASuffix(pendingDefaults_, pendingDefaultsAt_);
                if (!pendingDefaults_.empty()) {
                    defaultArgs_[sym] = pendingDefaults_;
                    defaultArgNamespace_[sym] = definitionNamespace_.empty() ? namespaceStack_ : definitionNamespace_;
                    // A member template's: read at the call with the class's parameters and its own (item 40).
                    if (!instantiatingParams_.empty())
                        defaultArgBindings_[sym] = DefaultBinding{ instantiatingParams_, instantiatingBinding_,
                                                                   instantiatingValues_, Packs() };
                }
            }
            if (consume(";")) {
                alive_.resize(alive_.size() - aliveParams);
                return;
            }
        }
        if (const std::vector<std::size_t> *set = overloadsOf(key)) {
            for (std::size_t k = 0; k < set->size() && member == nullptr; k++) {
                const Signature &f = functions_[(*set)[k]];
                // A member template's specialization defines itself and an ordinary member's definition defines no
                // specialization, though their parameters agree: `P(const P<Y> &)` at Y = P's own (item 30).
                if (f.fromTemplate != memberTemplateInst_) continue;
                if ((f.constThis == constThis || (impliedConst && f.isStaticMember)) &&
                    f.volatileThis == volatileThis &&
                    f.refThis == refThis && sameParameters(f.params, params)) {
                    member = &f;
                    memberStatic = f.isStaticMember;
                }
            }
        }
        if (member == nullptr)
            src_.fail(d.pos, "'" + d.qualifier + "' declares no member '" +
                             d.name + "' with these parameters and qualifiers");
        if (member->returns != d.type)
            src_.fail(d.pos, "'" + key + "' was declared to return '" +
                             member->returns->describe() + "' and this says '" +
                             d.type->describe() + "'");
        if (member->defined || member->defaultedOutside)
            src_.fail(d.pos, "'" + key + "' is defined twice");
        // **`S::S() = default;`**: a special member the class declared, given here the body the
        // implicit one would have - user-provided all the same, [dcl.fct.def.default]/4-5.
        if (defaultedHere) {
            takeSpecialDef();
            const std::string local = localOf(d.qualifier);
            const Type *p = params.size() == 1 ? params[0] : nullptr;
            const bool ofSelf = p != nullptr && p->isReference() &&
                                p->referent()->unqualified() == memberOf->unqualified();
            if (!((d.name == local && (params.empty() || ofSelf)) || (d.name == "~" + local && params.empty()) ||
                  (d.name == "operator=" && ofSelf)) || variadic)
                src_.fail(pendingSpecialAt_, "'" + key + "' is not a special member function, and only one of "
                                             "those can be defaulted - [dcl.fct.def.default]/1");
            for (std::size_t i = 0; i < defaults.size(); i++)
                if (defaults[i] != 0)
                    src_.fail(pendingSpecialAt_, "a defaulted function cannot have default arguments - "
                                                 "[dcl.fct.def.default]/1");
            Signature &m = const_cast<Signature &>(*member);
            m.defaultedOutside = true;
            m.used = true;
            expect(";");
            alive_.resize(alive_.size() - aliveParams);
            currentClass_ = nullptr;
            return;
        }
        // **This used to write `member->pos` into the *first* overload's entry.**
        const_cast<Signature *>(member)->defined = true;
        // From here a failure is the body's, and a trial around this instantiation reports it.
        if (memberTemplateInst_) memberTemplateBody_ = true;

        // **A static member's body gets no `this` slot**, which is the whole
        // of what makes it static once the name is settled.
        inStaticMember_ = member->isStaticMember;
        msVbInitSlot_ = -1;
        if (!member->isStaticMember) {
            // `this` takes the first slot, and its type carries the constness the member
            // was declared with - so a const member function cannot write through it, by
            // the ordinary rule that a const object's members are const.
            const Type *pointee = constThis ? types_.withConst(memberOf) : memberOf;
            if (volatileThis) pointee = types_.withVolatile(pointee);
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
        definingQualified_ = !absoluteName.empty();
        declareFunction(d.name, d.type, params, variadic, true, d.pos,
                        sc == StorageStatic);
        definingQualified_ = false;
        // Which function's body is about to be read, so that an access check inside it
        // can ask whether a class befriended *this* function. A member's is left empty:
        // the qualified form that would befriend a member is refused where written.
        currentFunction_ = lookupSignature(absoluteName.empty() ? d.name : absoluteName, params, variadic,
                                           d.pos).symbol;
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
    // **An inline function's static local is one object for the program**
    // ([basic.def.odr]/6, [dcl.inline]/4): ABI-named, in a COMDAT, for an
    // inline, in-class or template function, not an internal one.
    currentFunctionInline_ = (replayingInline_ || inlineDefinition) &&
                             !(memberOf != nullptr ? inUnnamedNamespace_ : internalLinkage(sc));
    tssSeq_ = 0;
    // A local class's held bodies are replayed inside the function that defines it, and they name
    // the class and its neighbours by their written names - `Local dbl() const` (1.27, 2026-10-21,
    // review of 1.25, item 3: "expected a type" at the return type, the table cleared here).
    if (!replayingInline_) localTypes_.clear();
    // Closures are numbered within the function that writes them, which is what clang does.
    lambdaCount_ = 0;
    // The mem-initializer list, [class.base.init], parsed here because `this` and the parameters are in scope and the body has not begun.
    std::vector<StmtPtr> memberInits;
    // For each of those statements, the member it finishes building, or -1 - [except.ctor]/3 needs it.
    std::vector<int> memberDone;
    // Which members this constructor's own list covers. Kept out here because
    // the initialisers the class wrote are applied to the rest, below, and the
    // list itself is scoped to the block that reads it.
    std::set<std::string> namedInInit;
    std::map<std::string, std::vector<ExprPtr> > baseArgs;
    std::map<std::string, std::vector<ExprPtr> > memberExprs;
    std::map<std::string, Init> memberLists;
    std::map<std::string, std::size_t> where;
    // **[intro.execution]/10: each mem-initialiser is a full-expression of its own**, so the
    // temporaries its arguments make stay with it and die once it has run - in declaration
    // order, below, not in the order the list is read. Left pending, they were never destroyed.
    std::map<std::string, std::vector<Temporary> > memberTemps, baseTemps;
    std::vector<Temporary> delegateTemps;
    auto clearGuards = [&](const std::vector<Temporary> &temps, std::vector<StmtPtr> &into) {
        for (const Temporary &t : temps)
            if (t.flag != 0) into.push_back(StmtPtr(new ExprStmt(setGuard(t.flag, 0))));
    };
    auto destroyOwn = [&](std::vector<Temporary> &temps, std::vector<StmtPtr> &into) {
        std::vector<Temporary> outer;
        outer.swap(pendingTemps_);
        pendingTemps_.swap(temps);
        flushTemporaries(into);
        pendingTemps_.swap(outer);
    };
    // The members written `: m()`, and for one with a constructor the index of the one
    // to run; functions_.size() says there is none. Out here with the other two: the
    // declaration-order walk below reads all three from outside that block.
    const std::size_t noConstructor = static_cast<std::size_t>(-1);
    std::map<std::string, std::size_t> valueInit;
    const bool isCtor = memberOf != nullptr && d.name == localOf(d.qualifier);
    // [class.base.init]/6: `: S(args)` names the class itself - the target constructor
    // does everything a list would, so the entry is the list's only one.
    bool delegates = false;
    bool namesVirtualBase = false;
    std::vector<ExprPtr> delegateArgs;
    std::size_t delegatePos = 0;
    std::size_t entries = 0;
    // **A constructor's function try block opens before its mem-initialisers**, which it covers; a
    // destructor's covers the members and bases it destroys after its body.
    const bool ctorTry = memberOf != nullptr && peek().is("try") &&
                         (isCtor || d.name == "~" + localOf(d.qualifier));
    const std::size_t ctorTryAt = peek().pos;
    if (ctorTry && memberOf->hasVirtualBase())
        src_.fail(ctorTryAt, "a function try block on a constructor or destructor of a class with a virtual "
                             "base is not supported yet - the complete object's one deals with those, outside this one");
    if (ctorTry) at_++;
    // **`throw(T...)` on the hosts and Windows is a filter around the whole body**, mem-initialisers
    // included, whose pads hand over the way a function try block's do; TI's table states the list.
    const bool specFilter = declaredSpec.written && !target_.specInTable();
    const bool specialHandsOver = ctorTry || (specFilter && memberOf != nullptr &&
                                              (isCtor || d.name == "~" + localOf(d.qualifier)));
    if (specialHandsOver && !ctorTry && memberOf->hasVirtualBase())
        src_.fail(declaredSpec.pos, "a 'throw(...)' list on a constructor or destructor of a class with a virtual "
                                    "base is not supported yet - the complete object's one deals with those");
    if (memberOf != nullptr && peek().is(":")) {
        if (!isCtor)
            src_.fail(peek().pos, "an initialiser list belongs to a "
                                  "constructor, and '" + d.name + "' is not one");
        at_++;
        // [basic.lookup.unqual]/8: the mem-initializers of `ns::S::S()` look in ns as its body does (1.82: they
        // were read where the definition is written, and `Kind::B` of ns was "not declared").
        inNamespace.enter(definedIn);
        // `: Bs()...`, `: Bs(std::forward<A>(a))...` - [temp.variadic]/4: one mem-initializer per member of
        // the pack, each read from a copy of the pattern with the pack bound to that member; nextEntry steps
        // to the next copy, and past the `...` after the last.
        struct Expansion {
            bool active;
            std::size_t from, end, members, m;
            std::vector<Shadow> undo;
        } expansion{ false, 0, 0, 0, 0, std::vector<Shadow>() };
        struct Unbind {
            Parser *p; std::vector<Shadow> *u;
            ~Unbind() { p->unbindTemplateParameters(*u); }
        } unbindExpansion{ this, &expansion.undo };
        auto nextEntry = [&]() -> bool {
            if (expansion.active) {
                unbindTemplateParameters(expansion.undo);
                expansion.undo.clear();
                if (++expansion.m < expansion.members) {
                    bindPatternMember(expansion.from, expansion.end, expansion.m, &expansion.undo);
                    return true;
                }
                expansion.active = false;
                at_ = expansion.end;
            }
            return consume(",");
        };
        for (;;) {
            if (!expansion.active) {
                // The entry's name, its arguments, and a `...` after them.
                std::size_t e = at_;
                int angle = 0, paren = 0;
                for (; e < tokens_.size() && tokens_[e].kind != TokenKind::End; e++) {
                    const Token &t = tokens_[e];
                    if (angle == 0 && paren == 0 && (t.is("(") || t.is("{"))) break;
                    if (paren == 0 && t.is("<")) angle++;
                    else if (paren == 0 && t.is(">")) angle--;
                    else if (paren == 0 && t.is(">>")) angle -= 2;
                    else if (t.is("(")) paren++;
                    else if (t.is(")")) paren--;
                }
                for (int depth = 0; e < tokens_.size() && tokens_[e].kind != TokenKind::End; e++) {
                    if (tokens_[e].is("(") || tokens_[e].is("{")) depth++;
                    else if ((tokens_[e].is(")") || tokens_[e].is("}")) && --depth == 0) break;
                }
                std::size_t members = 0;
                bool any = false;
                for (std::size_t i = at_; e + 1 < tokens_.size() && tokens_[e + 1].is("...") && i < e; i++) {
                    const auto pk = tokens_[i].kind == TokenKind::Ident ? packs_.find(tokens_[i].text) : packs_.end();
                    if (pk == packs_.end()) continue;
                    if (any && pk->second.types.size() != members)
                        src_.fail(tokens_[e + 1].pos, "the packs expanded by this '...' have different lengths");
                    members = pk->second.types.size();
                    any = true;
                }
                if (any && members == 0) {
                    at_ = e + 2;
                    if (!consume(",")) break;
                    continue;
                }
                if (any) {
                    expansion.active = true;
                    expansion.from = at_;
                    expansion.end = e + 2;
                    expansion.members = members;
                    expansion.m = 0;
                    bindPatternMember(expansion.from, expansion.end, 0, &expansion.undo);
                }
            }
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
            // **`: m{v}`, a braced mem-initialiser** ([class.base.init]/7):
            // `: m(v)`'s values plus what braces add - no narrowing for a
            // scalar, list-initialisation for a class.
            const bool braced = peek().is("{");
            const std::size_t bracePos = peek().pos;
            // An array member, or a class member given values, takes the braces as a list.
            if (braced && templateBase == nullptr) {
                const Member *listed = memberOf->findMember(entry);
                if (listed != nullptr && listed->declaredIn == nullptr && !listed->type->isReference() &&
                    (listed->type->isArray() ||
                     (listed->type->unqualified()->isStructOrUnion() && !peekAt(1).is("}")))) {
                    if (memberExprs.count(entry))
                        src_.fail(epos, "'" + entry + "' is initialised twice");
                    std::vector<Temporary> listTemps;
                    listTemps.swap(pendingTemps_);
                    memberLists[entry] = parseInitialiser();
                    listTemps.swap(pendingTemps_);
                    entries++;
                    memberExprs[entry] = std::vector<ExprPtr>();
                    memberTemps[entry] = std::move(listTemps);
                    namedInInit.insert(entry);
                    where[entry] = epos;
                    if (!nextEntry()) break;
                    continue;
                }
            }
            if (!braced) expect("(");
            const int frameBeforeArgs = frameSize_;
            std::vector<ExprPtr> args;
            std::vector<Temporary> argTemps;
            argTemps.swap(pendingTemps_);
            if (braced) {
                at_++;
                while (!peek().is("}")) {
                    if (peek().is("{"))
                        src_.fail(peek().pos, "a braced list inside a braced mem-initialiser is not "
                                              "supported yet - name the type, 'T{...}'");
                    args.push_back(assign());
                    if (!consume(",")) break;
                }
                expect("}");
            } else {
                parseArguments(args);
            }
            argTemps.swap(pendingTemps_);
            entries++;
            // What the braces ask beyond the parentheses: no narrowing of a scalar's value; a class
            // with an initializer_list constructor, an aggregate or an array is refused by name.
            auto bracedFor = [&](const Type *t, const std::string &what) {
                if (!braced || t == nullptr) return;
                if (t->isReference()) return;
                const Type *u = t->unqualified();
                if (u->isArray())
                    src_.fail(bracePos, "a braced mem-initialiser for the array '" + what + "' is not "
                                        "supported yet - give the elements their values in the body");
                if (u->isStructOrUnion()) {
                    const Type *il = nullptr;
                    if (initializerListConstructor(u, &il) != nullptr)
                        src_.fail(bracePos, "'" + what + "{...}' would list-initialise '" + u->describe() +
                                            "' through its initializer_list constructor, which a "
                                            "mem-initialiser does not do yet - write the list as an "
                                            "argument in parentheses");
                    if (!hasConstructors(u->tag()) && !args.empty())
                        src_.fail(bracePos, "'" + what + "{...}' would aggregate-initialise '" +
                                            u->describe() + "', which a mem-initialiser does not do "
                                            "yet - give the class a constructor, or assign its members "
                                            "in the body");
                    return;
                }
                if (args.size() == 1) checkNarrowing(t, *args[0], bracePos, "'" + what + "'");
            };
            {
                const Type *named = templateBase != nullptr ? templateBase : findTypedef(entry);
                if (entry == d.qualifier || entry == localOf(d.qualifier) ||
                    (named != nullptr && named->unqualified() == memberOf->unqualified())) {
                    bracedFor(memberOf, entry);
                    delegates = true;
                    delegateArgs = std::move(args);
                    delegateTemps = std::move(argTemps);
                    delegatePos = epos;
                    if (!nextEntry()) break;
                    continue;
                }
            }

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
                    bracedFor(bs[i].type, entry);
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
                baseTemps[baseKey] = std::move(argTemps);
                namesVirtualBase = namesVirtualBase || wasVirtualBase;
            } else if (const Member *m = memberOf->findMember(entry)) {
                bracedFor(m->type, entry);
                if (m->declaredIn != nullptr)
                    src_.fail(epos, "member initialiser '" + entry + "' names a member of a "
                                    "base of '" + d.qualifier + "' - the base's constructor "
                                    "initialises it, and a list may name only this class's "
                                    "own members and its direct bases");
                if (memberExprs.count(entry))
                    src_.fail(epos, "'" + entry + "' is initialised twice");
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
                    // No constructor is `noConstructor`, a value no index has. It was functions_.size() until
                    // 1.74 - "one past the last" as it stood here - and the members built before this one
                    // add functions: a constructor template instantiated for an earlier member took that very
                    // index, and `: first(std::forward<A>(a)...), second()` then ran it on an int `second`
                    // instead of zeroing it (pair's piecewise constructor, with a key that is a pair of a class).
                    std::size_t ctorIndex = noConstructor;
                    if (mc != nullptr && !mc->tag().empty() &&
                        hasConstructors(mc->tag())) {
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
                memberTemps[entry] = std::move(argTemps);
                namedInInit.insert(entry);
                where[entry] = epos;
            } else {
                // [temp.variadic]/5: a pack of bases is initialised with the '...' that expands it.
                if (packs_.count(entry) != 0)
                    src_.fail(epos, "'" + entry + "' is a parameter pack, and here it is written without the "
                                    "'...' that expands it");
                src_.fail(epos, "'" + entry + "' is neither a member of '" +
                                d.qualifier + "' nor a direct base of it");
            }
            if (!nextEntry()) break;
        }
        inNamespace.leave();
    }

    StmtPtr delegateCall;
    if (delegates) {
        // Pending while the call is built, which takes over what it passes by value.
        pendingTemps_.swap(delegateTemps);
        delegateCall = delegatingCall(d, memberOf, std::move(delegateArgs),
                                      delegatePos, entries);
        pendingTemps_.swap(delegateTemps);
    }

    // **Every member, in declaration order, by the first of three rules that applies to
    // it** - [class.base.init]/8, /9 and /11: named in the list, its own initialiser, or
    // a class with constructors default-constructed. A union's members are not built.
    if (memberOf != nullptr && isCtor && !delegates) {
        const std::vector<Member> &all = memberOf->members();
        // [class.base.init]/8: the list names at most one member of a union, named or anonymous, and
        // the others of that union are then left alone - no initialiser of their own, no default.
        std::set<const Type *> namedUnions;
        std::size_t unionEntries = 0;
        for (std::size_t i = 0; i < all.size(); i++) {
            if (all[i].declaredIn != nullptr || memberExprs.find(all[i].name) == memberExprs.end()) continue;
            if (memberOf->kind() == Kind::Union && unionEntries++ > 0)
                src_.fail(where[all[i].name], "initializing multiple members of union");
            if (all[i].anonUnion != nullptr && !namedUnions.insert(all[i].anonUnion).second)
                src_.fail(where[all[i].name], "initializing multiple members of union");
        }
        // The statements the member before this one added, and which member that was.
        std::size_t doneFrom = memberInits.size();
        int doneMember = -1;
        // What was pending before a listed member's own temporaries took its place.
        std::vector<Temporary> outerTemps;
        bool ownPending = false;
        auto settle = [&]() {
            if (ownPending) {
                flushTemporaries(memberInits);
                pendingTemps_.swap(outerTemps);
                outerTemps.clear();
                ownPending = false;
            }
            memberDone.resize(memberInits.size(), -1);
            if (doneMember >= 0 && memberInits.size() > doneFrom) memberDone.back() = doneMember;
        };
        for (std::size_t i = 0; i < all.size(); i++) {
            settle();
            doneFrom = memberInits.size();
            doneMember = static_cast<int>(i);
            const Member *m = &all[i];
            // A base's members are in this class's list too, and its constructor built
            // them; a list entry never names one ([class.base.init]/2), even by the name
            // this class's own member hides it with.
            if (m->declaredIn != nullptr) continue;
            std::map<std::string, std::vector<ExprPtr> >::iterator found =
                memberExprs.find(m->name);
            if (found == memberExprs.end()) {
                if (m->anonUnion != nullptr && namedUnions.count(m->anonUnion) != 0) continue;
                // **A member the list leaves out is a full-expression of its own too**: its default
                // member initialiser, or its constructor's default arguments - `allocator_type()` for an
                // unordered_map member - make temporaries that die once it is built (1.81: left pending,
                // they were destroyed at the end of the next inline member's first statement, on that
                // function's frame - a guard on its `this`, a destructor on its stack).
                std::vector<Temporary> beside;
                beside.swap(pendingTemps_);
                struct Back {
                    std::vector<Temporary> &pending, &beside;
                    ~Back() { pending.swap(beside); }
                } back{ pendingTemps_, beside };
                StmtPtr one = memberInitialiser(d.qualifier, memberOf, *m,
                                                thisOffset_, d.pos);
                std::vector<ExprPtr> none;
                if (one == nullptr && memberOf->kind() != Kind::Union && m->anonUnion == nullptr)
                    one = constructMember(d.qualifier, memberOf, *m,
                                          thisOffset_, none, d.pos, false);
                // [class.base.init]/8: a const or reference member the list leaves out
                // is ill-formed, since nothing after this can give it a value.
                if (one == nullptr && memberOf->kind() != Kind::Union &&
                    (m->type->isReference() ||
                     (m->type->isConst() && !constDefaultInitialisable(m->type))))
                    src_.fail(d.pos, "this constructor of '" + d.qualifier +
                                     "' does not initialise its " +
                                     (m->type->isReference() ? "reference" : "const") +
                                     " member '" + m->name + "', and nothing after "
                                     "it can - name it in the initialiser list");
                if (one != nullptr) memberInits.push_back(std::move(one));
                flushTemporaries(memberInits);
                continue;
            }
            std::size_t epos = where[m->name];
            {
                std::vector<Temporary> &own = memberTemps[m->name];
                clearGuards(own, memberInits);
                outerTemps.swap(pendingTemps_);
                pendingTemps_.swap(own);
                ownPending = true;
            }

            std::map<std::string, Init>::iterator listed = memberLists.find(m->name);
            if (listed != memberLists.end()) {
                memberInits.push_back(listInitialiseMember(d.qualifier, memberOf, *m, thisOffset_,
                                                           listed->second, epos));
                continue;
            }

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
                const bool hasCtor = vi->second != noConstructor;
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
                    // A default constructor whose parameters all have defaults takes them here:
                    // `: e()` for `explicit E(result_type s = default_seed)` ([class.ctor]/5).
                    const Signature ctor = functions_[vi->second];
                    std::vector<ExprPtr> defaults;
                    applyDefaults(ctor, defaults, epos);
                    std::vector<ExprPtr> one;
                    one.push_back(std::move(addr));
                    std::vector<const Type *> ps;
                    ps.push_back(ptr);
                    for (std::size_t k = 0; k < defaults.size() && k < ctor.params.size(); k++) {
                        one.push_back(std::move(defaults[k]));
                        ps.push_back(ctor.params[k]);
                    }
                    memberInits.push_back(StmtPtr(new ExprStmt(
                        completeCall(mt->tag(), ctor.symbol,
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
        settle();
    }

    // [dcl.constexpr]/3-4: the definition's parameter types, and a function's return type, are literal types;
    // a specialization that falls short is only not constexpr, [dcl.constexpr]/6.
    if (constexprFunction && instantiationKey_.empty() && (memberOf == nullptr || !memberOf->isSpecialization())) {
        for (const Type *p : params)
            if (plainlyNotLiteral(p))
                src_.fail(d.pos, "'" + d.name + "' is 'constexpr', and its parameter type '" + p->describe() +
                                 "' is not a literal type - [dcl.constexpr]/3");
        if (!isCtor && plainlyNotLiteral(d.type))
            src_.fail(d.pos, "'" + d.name + "' is 'constexpr', and its return type '" + d.type->describe() +
                             "' is not a literal type - [dcl.constexpr]/3");
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
    // A constexpr function's body stays the one `return` its value is worked out from.
    sretSlot_ = constexprFunction ? 0 : sretSlot;
    nrvoSlot_ = 0;
    nrvoName_.clear();

    int regSaveSlot = 0;
    if (variadic) {
        frameSize_ = alignTo(frameSize_, 16);
        frameSize_ += 176;
        regSaveSlot = frameSize_;
    }
    variadicBody_ = variadic;

    // Anything already alive belongs to an enclosing function.
    const std::size_t paramsFrom = alive_.size() - aliveParams;

    // A function try block is a body that is one `try` statement, [dcl.fct.def.general]/1.
    const bool functionTry = peek().is("try");

    atFunctionBody_ = true;
    // [except.spec]/9 needs this inside the body, and the declarator read it a
    // few hundred lines up. [except.spec]/14: a destructor with no specification has the implicit one's.
    inNoexceptFunction_ = declaredNoexcept ||
        (memberOf != nullptr && !d.name.empty() && d.name[0] == '~' && !destructorMayThrow(memberOf));
    // The count [except.spec]/9 asks is the body's: what reading the declarator counted - a specialization's
    // signature is deduced through calls of its own - is not a call this function makes (1.77: every
    // `noexcept` specialization of a function template had a handler calling std::terminate, std::move's too).
    mayThrow_ = 0;
    dtorEndLabel_ = memberOf != nullptr && !d.name.empty() && d.name[0] == '~'
                        ? "$dtorend" + std::to_string(refTemps_++) : std::string();
    dtorReturned_ = false;
    bodyCleanupFrom_ = paramsFrom;
    // A class can be defined inside a function and its members defined from
    // there, so what this function allocates is what it added to the list.
    const std::size_t guardsFrom = guardSlots_.size();
    StmtPtr body;
    // What every function does once its body is read - in a constructor's function try block, after its handlers.
    auto wrapUp = [&]() {
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
            // std::terminate, which calls the program's terminate handler ([except.terminate]/2) - until 1.71 this
            // pad called abort itself, and a handler set with set_terminate was never reached (the suite's nx01).
            padSteps.push_back(StmtPtr(new ExprStmt(terminateCall())));
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
    };
    // A constructor's own: its members, bases and vptrs in front of the body, each guarding the rest.
    auto assemble = [&]() {
        memberDone.resize(memberInits.size(), -1);
        // Members go in front of the body last first, each guarding the rest, [except.ctor]/3 - and in a
        // function try block every pad hands over to the handlers, so each owes all built before it.
        std::vector<Alive> built;
        for (std::size_t bn = 0; specialHandsOver && bn < memberOf->bases().size(); bn++)
            if (!memberOf->bases()[bn].isVirtual && destructorOf(memberOf->bases()[bn].type) != nullptr) {
                built.push_back(Alive{ "this", thisOffset_, memberOf });
                built.back().owner = memberOf;
                built.back().base = static_cast<int>(bn);
            }
        for (std::size_t i = 0; specialHandsOver && i < memberInits.size(); i++)
            if (memberDone[i] >= 0 && memberHasDestructor(memberOf->members()[memberDone[i]].type)) {
                built.push_back(Alive{ "this", thisOffset_, memberOf });
                built.back().owner = memberOf;
                built.back().member = memberDone[i];
            }
        auto builtBefore = [&](const Alive &a) {
            std::vector<Alive> before;
            for (const Alive &b : built) {
                if (b.base == a.base && b.member == a.member) break;
                before.push_back(b);
            }
            return before;
        };
        for (std::size_t i = memberInits.size(); i-- > 0; ) {
            if (memberDone[i] >= 0) {
                Alive member{ "this", thisOffset_, memberOf };
                member.owner = memberOf;
                member.member = memberDone[i];
                if (memberHasDestructor(memberOf->members()[memberDone[i]].type))
                    body = guardSubobject(std::move(body), member, d.pos, builtBefore(member));
            }
            std::vector<StmtPtr> withInit;
            withInit.push_back(std::move(memberInits[i]));
            withInit.push_back(std::move(body));
            body = StmtPtr(new Block(std::move(withInit)));
        }

        if (delegateCall != nullptr) {
            // [except.ctor]/2: once the target returns the object is complete, so the body's exception destroys it.
            if (destructorOf(memberOf) != nullptr) {
                Alive whole{ "this", thisOffset_, memberOf };
                whole.owner = memberOf;
                body = guardSubobject(std::move(body), whole, d.pos);
            }
            std::vector<StmtPtr> withTarget;
            clearGuards(delegateTemps, withTarget);
            withTarget.push_back(std::move(delegateCall));
            destroyOwn(delegateTemps, withTarget);
            withTarget.push_back(std::move(body));
            body = StmtPtr(new Block(std::move(withTarget)));
        }

        // **And in front of all of it, on Microsoft, the most-derived guard.**
        if (memberOf != nullptr && memberOf->hasVptr() && !delegates &&
            (d.name == localOf(d.qualifier) ||
             d.name == "~" + localOf(d.qualifier))) {
            std::vector<StmtPtr> withVptr = storeVptrs(d.qualifier, memberOf, thisOffset_);
            withVptr.push_back(std::move(body));
            body = StmtPtr(new Block(std::move(withVptr)));
        }

        // **[class.dtor]/8: a written destructor destroys the class's members
        // too**, after its body and before its bases.
        if (dtorReturned_) {
            std::vector<StmtPtr> ended;
            ended.push_back(std::move(body));
            ended.push_back(StmtPtr(new Label(dtorEndLabel_, StmtPtr(new Block({})))));
            body = StmtPtr(new Block(std::move(ended)));
        }
        dtorEndLabel_.clear();
        const bool chainedDtor = memberOf != nullptr && d.name == "~" + localOf(d.qualifier) &&
                                 destructorMayThrow(memberOf);
        if (chainedDtor) {
            body = destroySubobjects(std::move(body), memberOf, thisOffset_, d.pos);
        } else if (memberOf != nullptr && d.name == "~" + localOf(d.qualifier)) {
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
             memberOf != nullptr && !delegates && !chainedDtor && bn < memberOf->bases().size() &&
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
                // [class.copy]/12: a trivially copyable base copied by name, `: B(b)`, has no copy constructor
                // to call - its copy is the bytes, as `B c(b);` makes it.
                bool copyBytes = false;
                // The entry's temporaries are pending while its call is built, which takes over
                // what it passes by value; what is left is destroyed after the call.
                std::map<std::string, std::vector<Temporary> >::iterator ownTemps =
                    baseTemps.find(base->tag());
                const bool ownSwapped = building && ownTemps != baseTemps.end();
                if (ownSwapped) pendingTemps_.swap(ownTemps->second);
                if (building && named != baseArgs.end()) {
                    chosenArgs.swap(named->second);
                    const Type *from = chosenArgs.size() == 1 && chosenArgs[0]->type() != nullptr
                                           ? chosenArgs[0]->type()->unqualified() : nullptr;
                    copyBytes = from != nullptr && from->isStructOrUnion() && isGlvalue(*chosenArgs[0]) &&
                                (from == base->unqualified() || accessibleBaseOffset(from, base) >= 0) &&
                                copyConstructorOf(base) == nullptr && moveConstructorOf(base) == nullptr &&
                                !base->nonTrivialCopy();
                    if (!copyBytes) chosen = resolveOverload(key, chosenArgs, d.pos);
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
                if (building && !copyBytes) applyDefaults(chosen, chosenArgs, d.pos);

                std::string symbol = chosen.symbol;
                if (!target_.microsoftNames() && !copyBytes) {
                    std::string sub;
                    if (building) {
                        sub = baseConstructorSymbol(chosen, base);
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
                if (copyBytes) {
                    ExprPtr src = std::move(chosenArgs[0]);
                    const Type *from = src->type()->unqualified();
                    ExprPtr at(new Unary('&', std::move(src)));
                    at->setType(types_.pointerTo(from));
                    if (from != base->unqualified()) at = convert(std::move(at), basePtr);
                    // An empty base has no byte of its own to copy: on the Microsoft layout it lies at the
                    // object's end, and the size of its class would write over what follows.
                    ExprPtr copy;
                    if (emptyClass(base)) {
                        copy = std::move(at);
                    } else {
                        ExprPtr value(new Unary('*', std::move(at)));
                        value->setType(base);
                        ExprPtr there(new Unary('*', std::move(me)));
                        there->setType(base);
                        copy.reset(new Assign(std::move(there), std::move(value)));
                        copy->setType(base);
                    }
                    if (destructorOf(base) != nullptr) {
                        Alive one{ "this", thisOffset_, memberOf };
                        one.owner = memberOf;
                        one.base = static_cast<int>(which);
                        body = guardSubobject(std::move(body), one, d.pos, builtBefore(one));
                    }
                    std::vector<StmtPtr> wrapped;
                    if (ownSwapped) {
                        pendingTemps_.swap(ownTemps->second);
                        clearGuards(ownTemps->second, wrapped);
                    }
                    wrapped.push_back(StmtPtr(new ExprStmt(std::move(copy))));
                    if (ownSwapped) destroyOwn(ownTemps->second, wrapped);
                    wrapped.push_back(std::move(body));
                    body = StmtPtr(new Block(std::move(wrapped)));
                    continue;
                }
                std::vector<ExprPtr> args;
                if (!building && target_.microsoftNames()) me = msShifted(std::move(me), msThisAdjust(symbol));
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
                if (ownSwapped) pendingTemps_.swap(ownTemps->second);

                std::vector<StmtPtr> wrapped;
                if (building) {
                    // [except.ctor]/3: everything after this base's construction destroys it on unwind.
                    if (destructorOf(base) != nullptr) {
                        Alive one{ "this", thisOffset_, memberOf };
                        one.owner = memberOf;
                        one.base = static_cast<int>(which);
                        body = guardSubobject(std::move(body), one, d.pos, builtBefore(one));
                    }
                    if (ownSwapped) clearGuards(ownTemps->second, wrapped);
                    wrapped.push_back(StmtPtr(new ExprStmt(std::move(call))));
                    if (ownSwapped) destroyOwn(ownTemps->second, wrapped);
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
        if (msVbInitSlot_ >= 0 && memberOf != nullptr && !delegates &&
            d.name == localOf(d.qualifier)) {
            std::vector<Alive> vbs;
            std::vector<StmtPtr> guarded =
                guardedVirtualBaseInit(memberOf, d.qualifier, thisOffset_,
                                       msVbInitSlot_, d.pos, -1, false, &baseArgs, &vbs);
            body = withVirtualBases(std::move(guarded), vbs, std::move(body), d.pos);
        }
    };
    inNamespace.enter(definedIn);
    std::vector<std::string> specSymbols;
    for (const Type *t : declaredSpec.types) {
        std::string why;
        specSymbols.push_back(target_.microsoftNames() ? std::string() : typeInfoSymbolFor(t, declaredSpec.pos, &why));
        if (!target_.microsoftNames() && specSymbols.back().empty())
            src_.fail(declaredSpec.pos, "a 'throw(...)' list cannot name '" + t->describe() + "': " + why);
    }
    std::string spec = "$spec";
    for (const std::string &sym : specSymbols) spec += "," + sym;
    if (specFilter) {
        const std::size_t bodyAt = peek().pos;
        body = tryStatement(bodyAt, [&]() {
            body = ctorTry ? tryStatement(ctorTryAt, [&]() { body = block(); assemble(); return std::move(body); }, true)
                           : block(functionTry);
            if (!ctorTry) assemble();
            return std::move(body);
        }, false, spec, &declaredSpec.types);
        inNamespace.leave();
        wrapUp();
    } else if (ctorTry) {
        body = tryStatement(ctorTryAt, [&]() { body = block(); assemble(); return std::move(body); }, true);
        inNamespace.leave();
        wrapUp();
    } else {
        body = block(functionTry);
        inNamespace.leave();
        wrapUp();
        assemble();
    }

    // **A frame the Microsoft runtime ends the program in**: a `noexcept` function that calls something
    // carries a FuncInfo and its scratch word whatever it holds - cl's EHFlags 5 (Phase 6).
    const bool msFrame = target_.microsoftNames() && inNoexceptFunction_ && mayThrow_ > 0;
    const int msHelp = msFrame ? msUnwindHelp() : 0;
    int frame = alignTo(frameSize_, 16);
    const Type *emittedReturn = d.type->isReference()
                              ? types_.pointerTo(d.type->referent()) : d.type;
    if (definedSymbol.empty())
        definedSymbol = lookupSignature(absoluteName.empty() ? d.name : absoluteName, params,
                                        variadic, d.pos).symbol;
    // **Entered at a later base's vfptr** (Phase 6): cl reads the members past the adjustor, and moving
    // `this` back by it first is the same function to every caller.
    if (member != nullptr && !memberStatic && body != nullptr && msThisAdjust(definedSymbol) != 0) {
        const Type *selfType = types_.pointerTo(constThis ? types_.withConst(memberOf) : memberOf);
        const Type *chars = types_.pointerTo(types_.get(Kind::Char));
        ExprPtr me(Var::local("this", thisOffset_));
        me->setType(selfType);
        ExprPtr asChars(new Cast(chars, std::move(me)));
        asChars->setType(chars);
        ExprPtr back(new Num(-static_cast<long long>(msThisAdjust(definedSymbol))));
        back->setType(types_.intType());
        ExprPtr moved(new Binary(BinOp::Add, std::move(asChars), std::move(back)));
        moved->setType(chars);
        ExprPtr typed(new Cast(selfType, std::move(moved)));
        typed->setType(selfType);
        ExprPtr self(Var::local("this", thisOffset_));
        self->setType(selfType);
        ExprPtr set(new Assign(std::move(self), std::move(typed)));
        set->setType(selfType);
        std::vector<StmtPtr> both;
        both.push_back(StmtPtr(new ExprStmt(std::move(set))));
        both.push_back(std::move(body));
        body = StmtPtr(new Block(std::move(both)));
    }

    if (namesVirtualBase) virtualBaseInitialisers_.insert(definedSymbol);
    // Recorded before `body` is moved into the Function, where what the pointer names lives on; a
    // constexpr constructor is kept whole, also under the C2 name a derived class calls it by.
    if (isCtor && body != nullptr && (constexprFunction || constexprCtorSymbols_.count(definedSymbol))) {
        // [dcl.constexpr]/4: every non-static data member is initialised - by the list or its own initialiser.
        if (!delegates && memberOf->kind() != Kind::Union)
            for (const Member &m : memberOf->members())
                if (m.declaredIn == nullptr && !m.name.empty() && m.anonUnion == nullptr &&
                    namedInInit.count(m.name) == 0 && memberInit_.count(d.qualifier + "::" + m.name) == 0 &&
                    !m.type->innermostElement()->unqualified()->isStructOrUnion())
                    src_.fail(d.pos, "'constexpr' constructor of '" + d.qualifier + "' leaves the member '" +
                                     m.name + "' uninitialised - [dcl.constexpr]/4");
        ConstexprCtor ctor;
        ctor.body = body.get();
        ctor.thisSlot = thisOffset_;
        for (const Param &p : paramSlots)
            if (p.offset != thisOffset_) ctor.slots.push_back(p.offset);
        constexprCtors_[definedSymbol] = ctor;
        const std::size_t c1 = definedSymbol.find("C1E");
        if (c1 != std::string::npos) constexprCtors_[std::string(definedSymbol).replace(c1, 3, "C2E")] = ctor;
    } else if (constexprFunction && body != nullptr) {
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
    currentFunctionInline_ = false;
    localTypes_.clear();
    // **`static` on a member says which member, not which linkage.** [basic.link]/8: one named through an
    // unnamed class - `sz<decltype(anon_s)>` - has none outside this unit, its symbol another unit's too.
    bool unnamedArgument = definedSymbol.find("<unnamed-type-") != std::string::npos;
    for (std::size_t i = 1; i + 2 < definedSymbol.size() && !unnamedArgument; i++)
        unnamedArgument = definedSymbol[i] == '$' && definedSymbol[i + 1] == '_' &&
                          definedSymbol[i - 1] >= '0' && definedSymbol[i - 1] <= '9' &&
                          definedSymbol[i + 2] >= '0' && definedSymbol[i + 2] <= '9';
    const bool internal = unnamedArgument || (memberOf != nullptr ? inUnnamedNamespace_ : internalLinkage(sc));
    program.functions.push_back(Function(d.name, emittedReturn, std::move(paramSlots),
                                         std::move(body), frame,
                                         internal, sretSlot,
                                         variadic, regSaveSlot, d.pos,
                                         std::move(fnVars_)));
    program.functions.back().setSymbol(definedSymbol);
    // **-g: the name a debugger shows, as cl's PDB has it**: a specialization
    // with its arguments (`ns::biggest<int>`), a member under its class's whole
    // name (Type::displayTag); GRIDE's step filter `*!std::*` relies on it.
    if (!instantiationDisplay_.empty() && d.name == instantiationOf_)
        program.functions.back().setDisplayName(Type::displaySpelling(instantiationDisplay_));
    else if (memberOf != nullptr)
        program.functions.back().setDisplayName(Type::displaySpelling(
            memberOf->displayTag() + "::" +
            (memberTemplateInst_ && d.name == memberTemplateOf_ ? memberTemplateName_ : d.name)));
    // The definition side of the same question: a member's first parameter is
    // its `this`, and on the Microsoft ABI that is what the hidden return
    // pointer has to come *after*.
    program.functions.back().setHasThis(!d.qualifier.empty() && !inStaticMember_);
    program.functions.back().setHasLandingPads(functionHasPads_);
    program.functions.back().setNoexcept(inNoexceptFunction_);
    if (declaredSpec.written && target_.specInTable()) program.functions.back().setExceptionSpec(specSymbols);
    if (msFrame) program.functions.back().setMsUnwindHelp(msHelp);
    // Everything replayed from inside a class body - and every member of a
    // template specialization, which is replayed the same way - is implicitly
    // inline, so its definition may appear in several translation units.
    program.functions.back().setInline((replayingInline_ || inlineDefinition) &&
                                        !internal);
    // **[basic.def.odr]/3: an inline function need be defined only where it is odr-used**; a template's
    // members have a gate of their own, so only what the program wrote out is held for pruning.
    const std::size_t definedAt = program.functions.size() - 1;
    const bool discardable = (replayingInline_ || inlineDefinition) &&
        (memberOf == nullptr || !memberOf->unqualified()->isSpecialization());
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
        const std::map<std::string, std::string>::const_iterator tc2 =
            constructorC2_.find(program.functions.back().symbol());
        if (tc2 != constructorC2_.end()) c2 = tc2->second;
        if (!c2.empty() ||
            itaniumConstructorName(d.qualifier, findTypedef(d.qualifier),
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
                               -1, false, &baseArgs, delegates);
    for (std::size_t i = 0; i < delegationEntries_.size(); i++)
        if (delegationEntriesMade_.insert(delegationEntries_[i].entry).second)
            synthesizeDelegationEntry(memberOf, delegationEntries_[i], d.pos);
    delegationEntries_.clear();
    if (!splitD1.empty())
        synthesizeCompleteDtor(memberOf, splitD1, splitD2,
                               program.functions.back().isInline(), d.pos);
    for (std::size_t i = definedAt; discardable && i < program.functions.size(); i++)
        discardableInline_.insert(program.functions[i].symbol());
}

Program Parser::parse() {
    Program program;
    current_ = &program;
    while (peek().kind != TokenKind::End)
        topLevel(program);
    if (!mainUses_.empty() && mainDefinedHere())
        src_.fail(mainUses_[0], "'main' cannot be used within the program - [basic.start.main]/3");
    // **Each of these two can give the other more to do, so they alternate.**
    for (int pass = 0; pass < 64; pass++) {
        const std::size_t had = program.functions.size();
        instantiatePending();
        defineImplicitFunctions();
        if (program.functions.size() == had) break;
    }
    if (probeVariant_ >= 0) {
        probeUninstantiated(program);
        return program;
    }
    pruneExternalVtables(program);
    pruneUnchosenCandidates(program);
    finishDynamicInit(program);
    finishTlsInit(program);
    for (Function &f : program.functions) {
        const std::map<std::string, int>::const_iterator a = frameAligns_.find(f.symbol());
        if (a != frameAligns_.end()) f.setFrameAlign(a->second);
    }
    pruneUnusedInline(program);
    return program;
}

// A written namespace name - relative to the enclosing namespaces, or an alias at any
// component - as the qualified name of the namespace it means; empty where it means none.
std::string Parser::resolveNamespaceName(const std::string &written) const {
    std::vector<std::string> parts;
    std::string rest = written;
    const bool global = rest.compare(0, 2, "::") == 0;
    if (global) rest = rest.substr(2);
    for (std::string::size_type cut; (cut = rest.find("::")) != std::string::npos;
         rest = rest.substr(cut + 2))
        parts.push_back(rest.substr(0, cut));
    parts.push_back(rest);
    for (std::size_t i = namespaceStack_.size() + 1; i-- > 0; ) {
        if (global && i != 0) continue;
        std::string cur;
        for (std::size_t k = 0; k < i; k++) cur += namespaceStack_[k] + "::";
        bool ok = true;
        for (std::size_t p = 0; p < parts.size() && ok; p++) {
            cur += parts[p];
            std::map<std::string, std::string>::const_iterator a = namespaceAliases_.find(cur);
            if (a != namespaceAliases_.end()) cur = a->second;
            else if (namespaces_.find(cur) == namespaces_.end()) ok = false;
            if (p + 1 < parts.size()) cur += "::";
        }
        if (ok) return cur;
    }
    return "";
}

// `namespace A = N;`, the `=` consumed. [namespace.alias]: A names N from here to the end of
// the enclosing namespace. Every later `A::` in that reach is rewritten to N's name in place -
// one token's text, never a token added - so the string-keyed lookups meet N and never A.
void Parser::namespaceAlias(const std::string &name, std::size_t pos, bool inBlock) {
    std::string written = consume("::") ? "::" : "";
    written += expectIdent("a namespace name after '='");
    while (peek().is("::")) {
        at_++;
        written += "::" + expectIdent("a namespace name after '::'");
    }
    expect(";");
    const std::string target = resolveNamespaceName(written);
    if (target.empty())
        src_.fail(pos, "'" + written + "' is not a namespace, so there is nothing for "
                       "'namespace " + name + "' to be an alias of");
    // One in a block hides a namespace or class of the name around it, [basic.scope.hiding].
    if (!inBlock && (findTypedef(namespacePrefix() + name) != nullptr ||
                     namespaces_.count(namespacePrefix() + name)))
        src_.fail(pos, "'" + name + "' is already declared here, and a namespace alias "
                       "cannot reuse the name");
    if (inBlock) enterBlockEntry(namespaceAliases_, namespacePrefix() + name, target);
    else namespaceAliases_[namespacePrefix() + name] = target;

    // Inside the enclosing block an unqualified `A ::` means the alias; past its closing
    // brace `N :: A ::` does, for a target within N. A name after `::`, `.` or `->` is not it.
    auto plainAt = [&](std::size_t i) {
        if (tokens_[i].kind != TokenKind::Ident || tokens_[i].text != name) return false;
        if (i + 1 >= tokens_.size() || !tokens_[i + 1].is("::")) return false;
        return i == 0 || !(tokens_[i - 1].is("::") || tokens_[i - 1].is(".") ||
                           tokens_[i - 1].is("->") || tokens_[i - 1].is("namespace"));
    };
    int depth = 0;
    std::size_t i = at_;
    for (; i < tokens_.size() && tokens_[i].kind != TokenKind::End; i++) {
        if (tokens_[i].is("{")) depth++;
        else if (tokens_[i].is("}") && --depth < 0) break;
        else if (plainAt(i)) tokens_[i].text = target;
    }
    if (inBlock || namespaceStack_.empty()) return;
    const std::string prefix = namespacePrefix();
    if (target.compare(0, prefix.size(), prefix) != 0) return;
    const std::string tail = target.substr(prefix.size());
    for (; i < tokens_.size() && tokens_[i].kind != TokenKind::End; i++) {
        if (tokens_[i].kind != TokenKind::Ident || tokens_[i].text != name ||
            i + 1 >= tokens_.size() || !tokens_[i + 1].is("::") || i < 2 ||
            !tokens_[i - 1].is("::"))
            continue;
        std::size_t j = i - 1;
        bool spelled = true;
        for (std::size_t k = namespaceStack_.size(); spelled && k-- > 0; ) {
            spelled = j >= 1 && tokens_[j].is("::") &&
                      tokens_[j - 1].kind == TokenKind::Ident &&
                      tokens_[j - 1].text == namespaceStack_[k];
            if (spelled) j = j >= 2 ? j - 2 : 0;
        }
        if (spelled && !tokens_[j].is("::")) tokens_[i].text = tail;
    }
}

// [dcl.fct]/2: a declarator with a trailing return type has plain `auto` in front of it;
// `auto *f() -> int` and `const auto f() -> int` are ill-formed, as clang says.
void Parser::refuseTrailingBase(const Type *declared, std::size_t pos) {
    if (declared->kind() != Kind::Deduced)
        src_.fail(pos, "a function with a trailing return type is written with plain 'auto' "
                       "in front, and this one says '" + declared->describe() + "'");
}

// `-> T` after a parameter list, at_ on the arrow. With `paramsOpen` 0 the parameters are
// declared already; otherwise the list from that `(` is read again into a scope of its own,
// names and types only - an operand there is unevaluated, so no slot is taken.
const Type *Parser::readTrailingReturn(std::size_t paramsOpen) {
    const std::size_t arrow = peek().pos;
    expect("->");
    const std::size_t typeAt = at_;
    if (paramsOpen != 0) declareParametersAt(paramsOpen);
    at_ = typeAt;
    StorageClass rsc;
    const Type *returns = declarator(specifiers(&rsc), true).type;
    if (paramsOpen != 0) {
        locals_.resize(scopeStarts_.back());
        scopeStarts_.pop_back();
    }
    if (mentionsDeduced(returns))
        src_.fail(arrow, "a trailing return type cannot itself be deduced - 'auto' there "
                         "is C++14, and this compiler is C++11");
    return returns;
}

// The parameters of the list that opens at token `paramsOpen`, declared in a scope of their own for what is
// read after the list and may name them - a trailing return type ([basic.scope.block]/2), and since 1.77 the
// operand of `noexcept(...)` in the exception-specification. The caller puts `at_` back and closes the scope.
void Parser::declareParametersAt(std::size_t paramsOpen) {
    {
        enterScope();
        at_ = paramsOpen + 1;
        int index = 0;
        while (!consume(")")) {
            if (consume("...")) continue;
            // `A &&... a`: one parameter standing for the pack in a pattern, `a$0`... in an instantiation.
            std::vector<const Type *> packTypes;
            std::vector<std::string> packNames;
            if (packParameter(&packTypes, &packNames)) {
                for (std::size_t k = 0; k < packTypes.size(); k++, index++) {
                    if (k >= packNames.size() || packNames[k].empty()) continue;
                    locals_.push_back(Local{ packNames[k], 0, packTypes[k], false, std::string() });
                    locals_.back().isParameter = true;
                    locals_.back().trailingParam = index;
                }
                consume(",");
                continue;
            }
            StorageClass psc;
            const Type *pt = specifiers(&psc);
            Declared pd = declarator(pt, true);
            if (!pd.name.empty() && !pd.type->isVoid()) {
                if (pd.type->isArray()) pd.type = types_.pointerTo(pd.type->pointee());
                if (pd.type->kind() != Kind::PackExpansion) dropStalePack(pd.name);
                locals_.push_back(Local{ pd.name, 0, pd.type, false, std::string() });
                locals_.back().isParameter = true;
                locals_.back().trailingParam = index;
            }
            if (!pd.type->isVoid()) index++;
            if (consume("=")) skipDefaultArgument();
            consume(",");
        }
    }
}

// Past the cv, ref-qualifier and exception specification a function declarator may carry
// after its `)`, from token i: where a trailing `->` would stand.
std::size_t Parser::skipFunctionQualifiers(std::size_t i) const {
    for (;;) {
        if (i >= tokens_.size() - 1) return tokens_.size() - 1;
        const Token &t = tokens_[i];
        if (t.is("const") || t.is("volatile") || t.is("&") || t.is("&&")) { i++; continue; }
        if (!t.is("noexcept") && !t.is("throw")) return i;
        i++;
        if (!tokens_[i].is("(")) continue;
        for (int depth = 0; i < tokens_.size() - 1; i++) {
            if (tokens_[i].is("(")) depth++;
            else if (tokens_[i].is(")") && --depth == 0) { i++; break; }
        }
    }
}

// `auto` followed by `S::f(`, `S::~S(`, `Box<T>::get(` or `S::operator+(`: a qualified name
// a parameter list follows, which is a declarator-id and not the start of a type.
bool Parser::autoBeforeQualifiedName() const {
    std::size_t i = at_;
    bool qualified = false;
    while (i + 1 < tokens_.size() && tokens_[i].kind == TokenKind::Ident) {
        i++;
        if (tokens_[i].is("<")) {
            for (int depth = 0; i + 1 < tokens_.size(); i++) {
                if (depth > 0 && (tokens_[i].is("(") || tokens_[i].is("["))) { i = groupCloseAt(i); continue; }
                if (tokens_[i].is("<")) depth++;
                else if (tokens_[i].is(">") && --depth == 0) { i++; break; }
                else if (tokens_[i].is(">>") && (depth -= 2) <= 0) { i++; break; }
                else if (tokens_[i].is(";") || tokens_[i].is("{")) return false;
            }
        }
        if (!tokens_[i].is("::")) return qualified && tokens_[i].is("(");
        qualified = true;
        i++;
        if (tokens_[i].is("~")) i++;
        if (tokens_[i].is("operator")) return true;
    }
    return false;
}

// At a parameter list's `(`: whether a trailing `->` follows its `)` and qualifiers.
bool Parser::trailingArrowAhead() const {
    if (!peek().is("(")) return false;
    int depth = 0;
    for (std::size_t i = at_; i < tokens_.size() - 1; i++) {
        if (tokens_[i].is("(")) depth++;
        else if (tokens_[i].is(")") && --depth == 0)
            return tokens_[skipFunctionQualifiers(i + 1)].is("->");
    }
    return false;
}

// The call a delegating constructor makes - [class.base.init]/6 - to the target chosen by
// overload resolution, on `this`, before the body; its C1 on Itanium, as clang calls.
StmtPtr Parser::delegatingCall(const Declared &d, const Type *cls, std::vector<ExprPtr> args,
                               std::size_t pos, std::size_t entries) {
    if (entries != 1)
        src_.fail(pos, "a delegating constructor's initialiser list names '" +
                       localOf(d.qualifier) + "' and nothing else - the constructor it "
                       "delegates to builds the bases and members, [class.base.init]/6");
    const std::string key = constructorKey(cls->tag());
    Signature chosen = resolveOverload(key, args, pos);
    applyDefaults(chosen, args, pos);
    const Type *self = types_.pointerTo(cls);
    std::vector<ExprPtr> all;
    ExprPtr me(Var::local("this", thisOffset_));
    me->setType(self);
    all.push_back(std::move(me));
    std::vector<const Type *> params;
    params.push_back(self);
    // **A class with a virtual base**: the target builds them where this ctor
    // would - cl hands on the most-derived flag; on Itanium the call is to the
    // entry picking the target's C1 or C2 by the VTT (synthesizeDelegationEntry),
    // and this constructor's own C1 builds nothing.
    std::string symbol = chosen.symbol;
    if (cls->hasVirtualBase() && !target_.microsoftNames()) {
        const std::map<std::string, std::string>::const_iterator c2 = constructorC2_.find(chosen.symbol);
        const std::size_t c1 = symbol.find("C1E");
        if (c2 != constructorC2_.end()) symbol = c2->second;
        else if (c1 != std::string::npos) symbol.replace(c1, 3, "C2E");
        const std::size_t at = symbol.find("C2");
        if (at != std::string::npos) {
            DelegationEntry entry;
            entry.c1 = chosen.symbol;
            entry.c2 = symbol;
            entry.params = chosen.params;
            symbol.replace(at, 2, "C4");
            entry.entry = symbol;
            delegationEntries_.push_back(entry);
        }
        if (takesVtt(cls)) {
            ExprPtr vtt(Var::local(".vtt", vttSlot_));
            vtt->setType(vttType());
            all.push_back(std::move(vtt));
            params.push_back(vttType());
        }
    }
    for (std::size_t i = 0; i < chosen.params.size(); i++) {
        all.push_back(std::move(args[i]));
        params.push_back(chosen.params[i]);
    }
    if (cls->hasVirtualBase() && target_.microsoftNames()) {
        ExprPtr flag(Var::local(".initVBases", msVbInitSlot_));
        flag->setType(types_.intType());
        all.push_back(std::move(flag));
        params.push_back(types_.intType());
        return StmtPtr(new ExprStmt(completeCall(cls->tag(), symbol, nullptr, types_.get(Kind::Void), params,
                                                 false, pos, std::move(all), false, -1)));
    }
    return StmtPtr(new ExprStmt(completeCall(cls->tag(), symbol, nullptr,
                                             types_.get(Kind::Void), params, false, pos,
                                             std::move(all), false, 0)));
}
