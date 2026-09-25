// The parser: templates. Template parameters and the scope they are bound in,
// argument deduction, partial ordering and specialisation, and the token replay
// that turns a pattern into an instantiation. Rung 5.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>

// ------------------------------------------------------------------ templates
// **Rung 5.1: the table exists and nothing is instantiated.** `f<int>(x)` and
// `a<b>(c)` are the same tokens; only a name in the table opens an argument list.

// A `>` may be the front half of a `>>`. See the note on angleSplit_.
bool Parser::atClosingAngle() const {
    return peek().is(">") || peek().is(">>");
}

void Parser::takeClosingAngle() {
    if (consume(">")) return;
    if (!peek().is(">>"))
        src_.fail(peek().pos, "expected '>' to close this template argument list");
    if (angleSplit_ == at_) {
        angleSplit_ = static_cast<std::size_t>(-1);
        at_++;
        return;
    }
    angleSplit_ = at_;
}

// `template < class T, int N >`. Everything C++11 puts here that this rung
// does not implement is refused by name rather than misread.
void Parser::templateParameters(std::vector<TemplateParam> &params) {
    expect("<");
    if (atClosingAngle())
        src_.fail(peek().pos, "'template <>' is an explicit specialization, "
                              "and that is not supported yet");
    for (;;) {
        const std::size_t pos = peek().pos;
        if (peek().is("template"))
            src_.fail(pos, "a template template parameter is not supported yet");

        TemplateParam p;
        p.pos = pos;
        if (consume("class") || consume("typename")) {
            if (consume("...")) p.isPack = true;
            // C++ lets a parameter go unnamed. Nothing here can refer to
            // one, so it is a not-yet rather than a rule.
            if (peek().kind != TokenKind::Ident)
                src_.fail(peek().pos, "an unnamed template parameter is not "
                                      "supported yet");
            p.name = peek().text;
            at_++;
        } else {
            // A non-type parameter is written exactly like a function's, so
            // it is read exactly like one.
            for (std::size_t k = 0; peekAt(k).kind != TokenKind::End; k++) {
                if (peekAt(k).is(",") || peekAt(k).is(">") || peekAt(k).is(">>"))
                    break;
                if (peekAt(k).is("...")) {
                    src_.fail(peekAt(k).pos, "a non-type parameter pack is not "
                                             "supported yet - a pack of types "
                                             "is");
                }
            }
            StorageClass sc;
            Qualifiers quals;
            const Type *base = specifiers(&sc, &quals);
            if (sc != StorageNone)
                src_.fail(pos, "a template parameter has no storage class");
            Declared d = declarator(base);
            if (d.name.empty())
                src_.fail(pos, "an unnamed template parameter is not "
                               "supported yet");
            p.name = d.name;
            p.type = d.type;
        }
        // **A default argument, kept as tokens.** [temp.param]/9 lets a
        // parameter carry one, and a use that omits it replays these tokens
        // with the earlier parameters bound - which is how `N = M` sees `M`.
        if (consume("=")) {
            p.defBegin = at_;
            int depth = 0;
            for (;;) {
                const Token &t = peek();
                if (t.kind == TokenKind::End) break;
                if (depth == 0 && (t.is(",") || t.is(">") || t.is(">>"))) break;
                if (t.is("<") || t.is("(") || t.is("[")) depth++;
                else if (depth > 0 && (t.is(">") || t.is(")") || t.is("]"))) depth--;
                at_++;
            }
            p.defEnd = at_;
            if (p.defEnd == p.defBegin)
                src_.fail(peek().pos, "a default template argument has no value");
        }
        for (std::size_t i = 0; i < params.size(); i++)
            if (params[i].name == p.name)
                src_.fail(p.pos, "'" + p.name + "' is declared twice in this "
                                 "template parameter list");
        // **Only the last parameter may be a pack.** A pack takes every
        // argument that is left, so anything written after one could never be
        // given a value.
        if (!params.empty() && params.back().isPack)
            src_.fail(p.pos, "'" + params.back().name + "' is a parameter pack "
                             "and takes every argument that is left, so '" +
                             p.name + "' after it could never be given one");
        params.push_back(p);
        if (consume(",")) continue;
        break;
    }
    takeClosingAngle();
}

// The name the template is being given, and nothing else about it. A class
// template's is read straight off the keyword; **a function template's sits behind
// a return type that mentions the parameters**, so `T` denotes a stand-in first.
std::string Parser::templatedName(const std::vector<TemplateParam> &params,
                                  bool *isClass, std::string *qualifier,
                                  bool *construction) {
    qualifier->clear();
    if (peek().is("struct") || peek().is("class") || peek().is("union")) {
        if (peekAt(1).kind != TokenKind::Ident)
            src_.fail(peek().pos, "this class template has no name");
        *isClass = true;
        return peekAt(1).text;
    }
    *isClass = false;

    // **Read as a pattern, with the parameters standing for themselves.** The
    // stand-in used to be `int`, which was enough to find a name - but it would
    // instantiate `Box<int>` at a declaration that asks for no class at all.
    TemplateDecl scratch;
    scratch.params = params;
    scratch.afterParams = at_;
    std::vector<const Type *> binding(params.size());
    std::vector<long long> values(params.size(), 1);
    for (std::size_t i = 0; i < params.size(); i++)
        binding[i] = params[i].type == nullptr
                         ? types_.templateParam(static_cast<int>(i))
                         : params[i].type;
    std::string name;
    readTemplateDeclaration(scratch, binding, values, &name, qualifier, nullptr,
                            construction);
    if (name.empty())
        src_.fail(peek().pos, "this function template has no name");
    return name;
}

// **The parameters are bound to the argument list, and the tables are put back
// exactly as they were.** A type parameter becomes a type name and a non-type one
// an enumerator, which is what makes `T x` and `int a[N]` read with no new path.
void Parser::bindTemplateParameters(const std::vector<TemplateParam> &params,
                                    const std::vector<const Type *> &binding,
                                    const std::vector<long long> &values,
                                    const std::vector<std::vector<const Type *> > &packs,
                                    std::vector<Shadow> *undo) {
    for (std::size_t i = 0; i < params.size(); i++) {
        const TemplateParam &p = params[i];
        Shadow s;
        s.name = p.name;
        s.isType = p.type == nullptr;
        // **A pack is not a type name.** Nothing may write `Ts` on its own;
        // what reads it is `Ts...` and `sizeof...(Ts)`, and both want the
        // list rather than a type standing for it.
        if (p.isPack) {
            s.isPack = true;
            auto had = packs_.find(p.name);
            if (had != packs_.end()) {
                s.had = true;
                s.hadPack = had->second.types;
                s.hadNames = had->second.names;
            }
            PackBinding pb;
            if (i < binding.size() && binding[i] != nullptr &&
                binding[i]->kind() == Kind::TemplateParam)
                pb.types.push_back(binding[i]);       // reading a pattern
            else if (i < packs.size())
                pb.types = packs[i];
            packs_[p.name] = pb;
            undo->push_back(s);
            continue;
        }
        if (s.isType) {
            auto it = typedefIndex_.find(p.name);
            if (it != typedefIndex_.end()) { s.had = true; s.was = it->second; }
            typedefIndex_[p.name] = typedefs_.size();
            typedefs_.push_back(TypedefName{ p.name, binding[i] });
        } else if (i < binding.size() && binding[i] != nullptr &&
                   binding[i]->kind() == Kind::TemplateParam) {
            // **A non-type parameter read as a pattern**, the same signal a
            // pack uses: its binding is a Kind::TemplateParam rather than a
            // value, so a bare `V<N>` reads as a reference to N.
            s.isParamRef = true;
            auto it = nonTypePatternParams_.find(p.name);
            if (it != nonTypePatternParams_.end()) {
                s.hadParamRef = true; s.paramRefWas = it->second;
            }
            nonTypePatternParams_[p.name] = binding[i]->length();
            // The concrete value, as a second undo entry so both are restored.
            Shadow ev;
            ev.name = p.name;
            ev.isType = false;
            auto en = enumIndex_.find(p.name);
            if (en != enumIndex_.end()) { ev.had = true; ev.was = en->second; }
            enumIndex_[p.name] = enums_.size();
            enums_.push_back(EnumConst{ p.name, values[i] });
            undo->push_back(ev);
        } else {
            auto it = enumIndex_.find(p.name);
            if (it != enumIndex_.end()) { s.had = true; s.was = it->second; }
            enumIndex_[p.name] = enums_.size();
            enums_.push_back(EnumConst{ p.name, values[i] });
        }
        undo->push_back(s);
    }
}

void Parser::unbindTemplateParameters(const std::vector<Shadow> &undo) {
    for (std::size_t k = undo.size(); k-- > 0; ) {
        const Shadow &s = undo[k];
        if (s.isPack) {
            if (s.had) {
                packs_[s.name].types = s.hadPack;
                packs_[s.name].names = s.hadNames;
            } else {
                packs_.erase(s.name);
            }
            continue;
        }
        if (s.isType) {
            if (s.had) typedefIndex_[s.name] = s.was;
            else       typedefIndex_.erase(s.name);
        } else if (s.isParamRef) {
            if (s.hadParamRef) nonTypePatternParams_[s.name] = s.paramRefWas;
            else               nonTypePatternParams_.erase(s.name);
        } else {
            if (s.had) enumIndex_[s.name] = s.was;
            else       enumIndex_.erase(s.name);
        }
    }
}

// The declaration read again with the arguments in force. Nothing is
// registered: this answers what the signature *is*, and the caller decides
// what to do with it.
const Type *Parser::readTemplateDeclaration(const TemplateDecl &decl,
                                            const std::vector<const Type *> &binding,
                                            const std::vector<long long> &values,
                                            std::string *name,
                                            std::string *qualifier,
                                            const std::vector<std::vector<const Type *> > *packs,
                                            bool *construction) {
    const std::size_t resume = at_;
    // **Put back even if this throws.** Forming a signature is what a trial runs,
    // and a failed one must leave the parameter names unbound for the next
    // candidate - all that stands between a failure and a table still saying int.
    struct Unbind {
        Parser *p;
        std::vector<Shadow> undo;
        ~Unbind() { p->unbindTemplateParameters(undo); }
    } guard{ this, std::vector<Shadow>() };
    bindTemplateParameters(decl.params, binding, values,
                           packs != nullptr
                               ? *packs
                               : std::vector<std::vector<const Type *> >(),
                           &guard.undo);
    const bool wasPattern = patternOnly_;
    for (std::size_t i = 0; i < binding.size(); i++)
        if (binding[i] != nullptr && binding[i]->kind() == Kind::TemplateParam)
            patternOnly_ = true;

    at_ = decl.afterParams;
    StorageClass sc;
    Qualifiers quals;
    const Type *base = specifiers(&sc, &quals);
    Declared d = declarator(base);

    // **The declarator records where the parameter list is, and does not read it.**
    bool constructs = false;
    // `S K<T>::m(4);` - a static member defined out of line by a construction,
    // told from a parameter list as file scope tells them: a list is empty or
    // begins with a type.
    if (qualifier != nullptr && !d.qualifier.empty() && d.paramsAt == 0 &&
        peek().is("(") && d.type->isStructOrUnion() &&
        !d.type->tag().empty() &&
        overloadsOf(constructorKey(d.type->tag())) != nullptr) {
        const std::size_t save = at_;
        at_++;
        constructs = !(peek().is(")") || atDeclarationStart());
        at_ = save;
    }
    if (construction != nullptr) *construction = constructs;
    if (!constructs && (d.paramsAt != 0 || peek().is("("))) {
        if (d.paramsAt != 0) at_ = d.paramsAt;
        std::vector<const Type *> params;
        bool variadic = false;
        parameterTypes(params, variadic);
        d.type = types_.functionType(d.type, std::move(params), variadic);
    } else if (qualifier == nullptr) {
        src_.fail(d.pos, "'" + d.name + "' is a template and not a function, "
                         "and only function templates are supported yet");
    }

    patternOnly_ = wasPattern;
    at_ = resume;
    *name = d.name;
    if (qualifier != nullptr) *qualifier = d.qualifier;
    return d.type;
}

// From here to the `;` that ends the declaration, or to the `}` that closes
// the body. Nothing inside is looked at - that is what "no instantiation"
// means. Answers whether a body was there.
bool Parser::skipTemplatedDefinition(bool *sawInit, bool *sawParen) {
    bool body = false;
    int depth = 0;
    if (sawInit != nullptr) *sawInit = false;
    if (sawParen != nullptr) *sawParen = false;
    for (;;) {
        if (peek().kind == TokenKind::End)
            src_.fail(peek().pos, "this template's definition is never closed");
        // An `=` at depth zero is an initialiser: `template <class T> const R C<T>::k = R();`.
        if (sawInit != nullptr && depth == 0 && peek().is("=")) *sawInit = true;
        // A `(` at depth zero is a function's parameter list (or a construction);
        // without one, `=` or braces, the line defines a data member - [temp.static],
        // `template <class T> T Tm<T>::st;` (the cl review's A14).
        if (sawParen != nullptr && depth == 0 && peek().is("(")) *sawParen = true;
        if (peek().is("{")) { depth++; body = true; at_++; continue; }
        if (peek().is("}")) {
            at_++;
            if (--depth == 0) { consume(";"); return body; }
            continue;
        }
        if (peek().is(";") && depth == 0) { at_++; return body; }
        at_++;
    }
}

// Defined below, beside the pattern matching it belongs with.
static bool mentionsParam(const Type *t, std::size_t i);

// `template <class T> ...` at file scope. Answers false where the token is
// something else, so topLevel can ask without committing.
bool Parser::templateDeclaration() {
    if (!peek().is("template")) return false;

    TemplateDecl decl;
    decl.start = at_;
    at_++;
    if (!peek().is("<"))
        src_.fail(peek().pos, "explicit instantiation is not supported yet");
    if (peekAt(1).is(">")) return explicitSpecialization();
    templateParameters(decl.params);

    decl.afterParams = at_;
    decl.pos = peek().pos;

    // **An alias template is C++11 and begins with `using`** - [temp.alias].
    // It is asked before the scan below, which would see an '=' with no '('
    // in front of it and call a conforming C++11 program C++14.
    if (peek().is("using"))
        src_.fail(peek().pos, "an alias template - 'template <class T> using "
                              "X = ...;' - is not supported yet, though it is "
                              "C++11: a class template with a member typedef "
                              "says the same thing here");

    // **A variable template is C++14, and it is told from the two C++11
    // declarations by a token scan**: a class or function reaches '(' or a class
    // key before any '=', and an out-of-line member writes '::' before its own.
    if (!peek().is("struct") && !peek().is("class") && !peek().is("union")) {
        int depth = 0;
        for (std::size_t i = at_; i < tokens_.size(); i++) {
            const Token &t = tokens_[i];
            if (depth == 0) {
                if (t.is("(") || t.is("::") || t.is("{") || t.is(";")) break;
                if (t.is("="))
                    src_.fail(t.pos, "a variable template is C++14, and this "
                                     "compiler is C++11 - a class template "
                                     "with a static member says the same "
                                     "thing here");
            }
            if (t.is("[") || t.is("<")) depth++;
            else if (t.is("]") || t.is(">")) depth--;
            if (depth < 0) break;
        }
    }

    // Its own step, and refused by name until then: the declarator reads a class
    // *name* before the `::`, and reading a template-id there is what an
    // out-of-line constructor needs. A member function has a return type instead.
    std::string special;
    if (atOutOfLineSpecial(&special))
        src_.fail(decl.pos, "a " + special + " of a class template written "
                            "outside the class is not supported yet - write "
                            "it inside the class");

    // **`template <class T> struct Box<T *>` - a partial specialization.** It is
    // told from the primary by the `<` after the name: a class template being
    // *declared* has nothing there, and one already declared is being specialized.
    if ((peek().is("struct") || peek().is("class") || peek().is("union")) &&
        peekAt(1).kind == TokenKind::Ident && peekAt(2).is("<")) {
        auto primary = templates_.find(peekAt(1).text);
        if (primary == templates_.end() || !primary->second.isClass)
            src_.fail(peekAt(1).pos, "'" + peekAt(1).text + "' is not a class "
                                     "template, so there is nothing here to "
                                     "specialize");
        TemplateDecl::Partial ps;
        ps.params = decl.params;
        ps.pos = decl.pos;
        at_ += 2;
        partialArguments(&ps, primary->second.params);
        // **`:` as well as `{`.** A specialization may have a base clause and a
        // recursive variadic one always does - `struct A<T, R...> : A<R...>`. The
        // replay goes through structOrUnionSpecifier, which reads both in order.
        if (!peek().is("{") && !peek().is(":"))
            src_.fail(peek().pos, "a partial specialization is a definition, "
                                  "and this one has no body");
        ps.bodyAt = at_;
        for (std::size_t i = 0; i < ps.params.size(); i++) {
            bool mentioned = false;
            for (std::size_t k = 0; k < ps.args.size(); k++)
                if (ps.args[k].isPackExpansion) {
                    // `R...` names its parameter and holds no pattern to
                    // walk, so it is asked about directly rather than through
                    // mentionsParam - whose argument would be null.
                    if (ps.args[k].param == i) mentioned = true;
                } else if (!ps.args[k].isType) {
                    if (ps.args[k].isParam && ps.args[k].param == i) mentioned = true;
                } else if (mentionsParam(ps.args[k].type, i)) {
                    mentioned = true;
                }
            if (!mentioned)
                src_.fail(ps.params[i].pos, "'" + ps.params[i].name + "' is "
                          "never used in this specialization's arguments, so "
                          "nothing could ever work it out");
        }
        at_ = decl.afterParams;
        skipTemplatedDefinition();
        primary->second.partials.push_back(ps);
        return true;
    }

    std::string qualifier;
    decl.classKey = peek().is("class");
    bool constructs = false;
    decl.name = templatedName(decl.params, &decl.isClass, &qualifier, &constructs);
    // Where it was written, for the manglers - the table's key stays bare.
    decl.ns = namespacePrefix();
    at_ = decl.afterParams;
    bool sawInit = false, sawParen = false;
    const bool defined = skipTemplatedDefinition(&sawInit, &sawParen);

    // **A member of a class template defined outside it belongs to the class**, not
    // to a template of its own. The declarator already reads a qualified name; what
    // is new is that the qualifier is a template-id, naming the pattern.
    if (!qualifier.empty()) {
        const Type *of = findTypedef(qualifier);
        if (of == nullptr || !of->isSpecialization())
            src_.fail(decl.pos, "'" + qualifier + "' is not a class template, "
                                "so this defines a member of nothing");
        auto owner = templates_.find(of->templateName());
        if (owner == templates_.end())
            src_.fail(decl.pos, "'" + of->templateName() + "' is not a class "
                                "template");
        // The template's own name, not the qualifier: that is the pattern's
        // internal tag and holds a `$` no reader ever wrote.
        const bool dataNoInit = !defined && !sawInit && !constructs && !sawParen;
        if (!defined && !sawInit && !constructs && !dataNoInit)
            src_.fail(decl.pos, "'" + of->templateName() + "::" + decl.name +
                                "' is declared here and not defined - a member "
                                "is declared inside its class");
        TemplateDecl::OutOfLine ool;
        ool.start = decl.afterParams;
        ool.member = decl.name;
        ool.destructor = !decl.name.empty() && decl.name[0] == '~';
        ool.isData = !defined && (sawInit || constructs || dataNoInit);
        owner->second.outOfLine.push_back(ool);
        return true;
    }

    decl.defined = defined;
    // **Function templates overload; class templates do not.** A function
    // template of a name already seen is another overload, kept beside the
    // first; a call tries each by deduction.
    if (!decl.isClass) {
        if (decl.defined) fnTemplates_[decl.name].push_back(decl);
        auto it = templates_.find(decl.name);
        if (it == templates_.end()) {
            templates_[decl.name] = decl;
        } else if (decl.defined && !it->second.defined && !it->second.isClass) {
            decl.outOfLine = it->second.outOfLine;
            it->second = decl;
        }
        return true;
    }

    // A template may be declared and then defined. The definition is the one
    // worth keeping, since instantiating is replaying its tokens - but any
    // out-of-line members gathered against the declaration come with it.
    auto it = templates_.find(decl.name);
    if (it == templates_.end()) {
        templates_[decl.name] = decl;
    } else if (decl.defined && !it->second.defined) {
        decl.outOfLine = it->second.outOfLine;
        it->second = decl;
    } else if (decl.defined) {
        src_.fail(decl.pos, "'" + decl.name + "' is already a template, and "
                            "two class templates of one name are not supported "
                            "yet - a specialization is written 'template <> "
                            "struct " + decl.name + "<...>'");
    }
    return true;
}

// `template <> struct Box<int> { ... };` - rung 5.6. A class written out for one
// argument list: the tag is `Box<int>` as the template would have made it, so every
// lookup and mangling is the same. **The list is read against the primary's.**
bool Parser::explicitSpecialization() {
    const std::size_t pos = peek().pos;
    expect("<");
    takeClosingAngle();

    if (!peek().is("struct") && !peek().is("class") && !peek().is("union"))
        src_.fail(peek().pos, "an explicit specialization of a function "
                              "template is not supported yet - this one is not "
                              "a class");
    const Kind kind = peek().is("union") ? Kind::Union : Kind::Struct;
    const bool isClass = peek().is("class");
    at_++;

    if (peek().kind != TokenKind::Ident)
        src_.fail(peek().pos, "this specialization names no class");
    const std::string name = peek().text;
    auto primary = templates_.find(name);
    if (primary == templates_.end() || !primary->second.isClass)
        src_.fail(peek().pos, "'" + name + "' is not a class template, so "
                              "there is nothing here to specialize");
    at_++;
    if (!peek().is("<"))
        src_.fail(peek().pos, "'" + name + "' is a class template and a "
                              "specialization of it needs its arguments");

    std::vector<const Type *> binding;
    std::vector<long long> values;
    std::vector<TemplateArg> args;
    templateArguments(primary->second, &binding, &values, &args);

    const std::string tag = specializationKey(name, args);
    // **Too late is an error, not a redefinition.** [temp.expl.spec]: a
    // specialization has to be declared before the first use that would instantiate
    // the template, or two different classes have been given one name.
    if (findTypedef(tag) != nullptr)
        src_.fail(pos, "'" + tag + "' has already been used further up, so "
                       "specializing it here is too late - the specialization "
                       "goes before the first use");
    // **A base-clause is a body too.**
    if (!peek().is("{") && !peek().is(":") && !peek().is("final"))
        src_.fail(peek().pos, "an explicit specialization is a definition, and "
                              "this one has no body");

    classInstantiationTag_ = tag;
    classInstantiationOf_ = name;
    instantiatingArgs_ = args;
    instantiatingNamespace_ = primary->second.ns;
    const Type *made = structOrUnionSpecifier(kind, isClass);
    classInstantiationTag_.clear();
    classInstantiationOf_.clear();
    instantiatingParams_.clear();
    instantiatingBinding_.clear();
    instantiatingValues_.clear();
    instantiatingArgs_.clear();

    declareTypeName(tag, made);
    expect(";");
    return true;
}

// The tokens read without being consumed. skipTemplateArguments is what walks
// the argument list, so the `>>` split has to be put back too.
bool Parser::atOutOfLineSpecial(std::string *what) {
    if (peek().kind != TokenKind::Ident) return false;
    auto t = templates_.find(peek().text);
    if (t == templates_.end() || !t->second.isClass || !peekAt(1).is("<"))
        return false;

    const std::string name = peek().text;
    const std::size_t resume = at_;
    const std::size_t wasSplit = angleSplit_;
    at_++;
    skipTemplateArguments();

    bool yes = false;
    if (peek().is("::")) {
        std::size_t n = 1;
        if (peekAt(n).is("~")) { n++; *what = "destructor"; }
        else                   { *what = "constructor"; }
        if (peekAt(n).kind == TokenKind::Ident && peekAt(n).text == name &&
            peekAt(n + 1).is("(")) yes = true;
    }
    at_ = resume;
    angleSplit_ = wasSplit;
    return yes;
}

// The argument list, read only far enough to step over it - and stepping over it is
// what proves the `>>` split, since `Box<Box<int>>` cannot be got past any other
// way. A nested list is recognised by its name being a template.
void Parser::skipTemplateArguments() {
    expect("<");
    for (;;) {
        if (peek().kind == TokenKind::End)
            src_.fail(peek().pos, "this template argument list is never closed");
        if (atClosingAngle()) { takeClosingAngle(); return; }
        if (peek().kind == TokenKind::Ident && isTemplateName(peek().text) &&
            peekAt(1).is("<")) {
            at_++;
            skipTemplateArguments();
            continue;
        }
        // A parenthesised argument may hold a `>` that closes nothing.
        if (peek().is("(")) {
            int depth = 0;
            do {
                if (peek().kind == TokenKind::End)
                    src_.fail(peek().pos, "this template argument list is "
                                          "never closed");
                if (peek().is("(")) depth++;
                else if (peek().is(")")) depth--;
                at_++;
            } while (depth > 0);
            continue;
        }
        at_++;
    }
}

// `<int, 3>` at a use, read against the parameter list it is for. A type parameter
// takes a type-id and a non-type one a constant expression, so which is which is
// decided by the template and never by the shape of what is written.
void Parser::templateArguments(const TemplateDecl &decl,
                               std::vector<const Type *> *binding,
                               std::vector<long long> *values,
                               std::vector<TemplateArg> *args,
                               std::vector<std::vector<const Type *> > *packs) {
    expect("<");
    const bool wasInArgs = inTemplateArgs_;
    inTemplateArgs_ = true;
    if (packs != nullptr) packs->assign(decl.params.size(),
                                        std::vector<const Type *>());
    // **Defaults are replayed with the earlier parameters bound.**
    std::vector<Shadow> undo;
    struct Unbind {
        Parser *p; std::vector<Shadow> *u;
        ~Unbind() { p->unbindTemplateParameters(*u); }
    } unbind{ this, &undo };

    for (std::size_t i = 0; i < decl.params.size(); i++) {
        const TemplateParam &p = decl.params[i];
        TemplateArg a;

        // If the arguments have run out, the rest must carry defaults. A comma
        // is due only before a real argument, so it is consumed here and not at
        // the closing angle.
        bool defaulted = !p.isPack && atClosingAngle();
        if (!defaulted && i > 0 && !consume(","))
            src_.fail(peek().pos, "'" + decl.name + "' takes " +
                                  std::to_string(decl.params.size()) +
                                  " template arguments and this gives " +
                                  std::to_string(i));
        if (defaulted) {
            if (p.defBegin == 0)
                src_.fail(peek().pos, "'" + decl.name + "' takes " +
                                      std::to_string(decl.params.size()) +
                                      " template arguments and this gives " +
                                      std::to_string(i));
            // The first default binds every parameter before it, so its tokens
            // can name them; each default binds itself for the next one.
            if (undo.empty())
                for (std::size_t k = 0; k < i; k++) {
                    std::vector<TemplateParam> one(1, decl.params[k]);
                    std::vector<const Type *> ob(1, (*binding)[k]);
                    std::vector<long long> ov(1, (*values)[k]);
                    std::vector<std::vector<const Type *> > op(1, std::vector<const Type *>());
                    bindTemplateParameters(one, ob, ov, op, &undo);
                }
            const std::size_t saved = at_;
            at_ = p.defBegin;
            if (p.type == nullptr) {
                StorageClass sc; Qualifiers quals;
                const Type *base = specifiers(&sc, &quals);
                Declared d = declarator(base, true);
                binding->push_back(d.type); values->push_back(0);
                a.isType = true; a.type = d.type;
            } else {
                const long long v = constantExpression("a default template argument");
                binding->push_back(p.type); values->push_back(v);
                a.isType = false; a.type = p.type; a.value = v;
            }
            at_ = saved;
            args->push_back(a);
            std::vector<TemplateParam> one(1, p);
            std::vector<const Type *> ob(1, binding->back());
            std::vector<long long> ov(1, values->back());
            std::vector<std::vector<const Type *> > op(1, std::vector<const Type *>());
            bindTemplateParameters(one, ob, ov, op, &undo);
            continue;
        }
        // **A pack takes everything that is left**, including nothing. It is
        // the last parameter by construction, so there is no ambiguity about
        // where it stops: the closing angle stops it.
        if (p.isPack) {
            a.isPack = true;
            a.isType = true;
            // **`first` and not `a.pack.empty()`.** An expansion may contribute
            // nothing - `A<R...>` where R is empty is how a recursion ends - so
            // emptiness cannot say whether a comma is due.
            bool first = true;
            while (!atClosingAngle()) {
                if (!first) expect(",");
                first = false;
                // **`R...` - one pack expanded into another's argument list**,
                // which is what makes a recursive variadic class possible: each
                // step passes on all but the head until the empty case stops it.
                if (peek().kind == TokenKind::Ident && peekAt(1).is("...")) {
                    auto pk = packs_.find(peek().text);
                    if (pk != packs_.end()) {
                        const std::vector<const Type *> &members =
                            pk->second.types;
                        // In a pattern the pack stands for itself and there is
                        // nothing to splice; reading one here would put a
                        // Kind::TemplateParam into a real argument list.
                        if (members.size() == 1 &&
                            members[0]->kind() == Kind::TemplateParam)
                            src_.fail(peek().pos,
                                      "expanding a pack into another "
                                      "template's argument list needs the "
                                      "pack's members, and here it stands for "
                                      "itself");
                        at_ += 2;
                        for (std::size_t k = 0; k < members.size(); k++)
                            a.pack.push_back(members[k]);
                        continue;
                    }
                }
                StorageClass sc;
                Qualifiers quals;
                const Type *base = specifiers(&sc, &quals);
                Declared d = declarator(base, true);
                if (!d.name.empty())
                    src_.fail(d.pos, "a template argument is a type here, and "
                                     "this names something");
                a.pack.push_back(d.type);
            }
            binding->push_back(nullptr);
            values->push_back(0);
            if (packs != nullptr) (*packs)[i] = a.pack;
            args->push_back(a);
            break;
        }
        if (p.type == nullptr) {
            StorageClass sc;
            Qualifiers quals;
            const Type *base = specifiers(&sc, &quals);
            Declared d = declarator(base, true);
            if (!d.name.empty())
                src_.fail(d.pos, "a template argument is a type here, and this "
                                 "names something");
            binding->push_back(d.type);
            values->push_back(0);
            a.isType = true;
            a.type = d.type;
        } else {
            if (!p.type->isInteger())
                src_.fail(p.pos, "a non-type template parameter of type '" +
                                 p.type->describe() + "' is not supported yet - "
                                 "it must be an integer type");
            // **`V<N>` while a signature is read for deduction**: a bare name
            // that is a pattern non-type parameter is a reference to it, not a
            // value to fold. Anything else is an ordinary constant expression.
            if (peek().kind == TokenKind::Ident &&
                nonTypePatternParams_.count(peek().text) != 0 &&
                (peekAt(1).is(",") || peekAt(1).is(">") || peekAt(1).is(">>"))) {
                a.isType = false;
                a.isParam = true;
                a.paramIndex = nonTypePatternParams_[peek().text];
                a.type = p.type;
                binding->push_back(p.type);
                values->push_back(0);
                at_++;
            } else {
                const long long v = constantExpression("a template argument");
                binding->push_back(p.type);
                values->push_back(v);
                a.isType = false;
                a.type = p.type;
                a.value = v;
            }
        }
        args->push_back(a);
    }
    if (!atClosingAngle())
        src_.fail(peek().pos, "'" + decl.name + "' takes " +
                              std::to_string(decl.params.size()) +
                              " template arguments and this gives more");
    takeClosingAngle();
    inTemplateArgs_ = wasInArgs;
}

std::string Parser::specializationKey(const std::string &name,
                                      const std::vector<TemplateArg> &args) const {
    std::string key = name + "<";
    for (std::size_t i = 0; i < args.size(); i++) {
        if (i > 0) key += ",";
        if (args[i].isPack) {
            key += "{";
            for (std::size_t k = 0; k < args[i].pack.size(); k++) {
                if (k > 0) key += ",";
                key += args[i].pack[k]->describe();
            }
            key += "}";
        }
        else if (args[i].isParam)
            // A non-type parameter reference - `V<N>` in a pattern - keyed by
            // which parameter, or `V<N>` and `V<M>` would be one type.
            key += "$P" + std::to_string(args[i].paramIndex);
        else if (!args[i].isType) key += std::to_string(args[i].value);
        // A pattern's argument is a template parameter, and describe() would
        // put a space in a tag every table is keyed by.
        else if (args[i].type->kind() == Kind::TemplateParam)
            key += "$T" + std::to_string(args[i].type->length());
        else key += args[i].type->describe();
    }
    return key + ">";
}

// The specialization these arguments ask for, made if it is new. **The two ABIs are
// handed two different things**: Itanium the pattern, since its name spells `T_`,
// and Microsoft the substituted signature. So the declaration is read twice.
const Parser::Signature &
Parser::instantiate(const TemplateDecl &decl,
                    const std::vector<const Type *> &binding,
                    const std::vector<long long> &values,
                    const std::vector<TemplateArg> &args, std::size_t pos,
                    const std::vector<std::vector<const Type *> > &packs) {
    if (!decl.defined)
        src_.fail(pos, "'" + decl.name + "' is declared but never defined, so "
                       "there is nothing to instantiate");

    std::string name;
    const Type *fn = readTemplateDeclaration(decl, binding, values, &name,
                                             nullptr, &packs);

    // **Two function-template overloads can share a name and arguments and differ only in
    // signature** - operator*(V<N>, double) and operator*(double, V<N>) both key operator*<3>
    // without the parameter types, so the second was deduped to the first and the wrong one called.
    const std::string display = specializationKey(decl.name, args);
    std::string key = display;
    for (std::size_t i = 0; i < fn->params().size(); i++)
        key += "#" + fn->params()[i]->describe();

    if (const std::vector<std::size_t> *had = overloadsOf(key))
        return functions_[(*had)[0]];

    // **The pattern the Itanium name is spelled from** keeps every parameter
    // as a reference to itself - a type parameter as Kind::TemplateParam, a
    // non-type one as the param-ref `V<N>` reads to.
    std::vector<const Type *> pattern(decl.params.size());
    for (std::size_t i = 0; i < decl.params.size(); i++)
        pattern[i] = types_.templateParam(static_cast<int>(i));
    std::string patternName;
    const Type *patternFn =
        readTemplateDeclaration(decl, pattern, values, &patternName);

    std::string symbol, why;
    const bool ok = target_.microsoftNames()
        ? microsoftTemplateFunctionName(decl.name, fn, args, &symbol, &why)
        : itaniumTemplateFunctionName(decl.name, patternFn, args, false,
                                      &symbol, &why);
    if (!ok)
        src_.fail(pos, "'" + key + "' cannot be given a name the linker can "
                       "hold: " + why);

    Specialization sp;
    sp.key = key;
    sp.name = decl.name;
    sp.params = decl.params;
    sp.packs = packs;
    sp.symbol = symbol;
    sp.fn = fn;
    sp.binding = binding;
    sp.values = values;
    sp.start = decl.afterParams;
    sp.pos = pos;
    specializations_.push_back(sp);

    // **Under two keys, on purpose.** "twice<int>" is what the replayed definition
    // declares and what a repeat of the same arguments finds; "twice" is what
    // overload resolution must see, a specialization competing with the ordinary.
    const std::size_t at = functions_.size();
    functionIndex_[key].push_back(at);
    functionIndex_[decl.name].push_back(at);
    functions_.push_back(Signature{ display, symbol, fn->returns(), fn->params(),
                                    fn->isVariadicFn(), false, pos, false,
                                    std::string(), false, Access::Public });
    functions_.back().fromTemplate = true;
    // **The defaults the pattern's parameter list just read.**
    if (!pendingDefaults_.empty()) {
        defaultArgs_[symbol] = pendingDefaults_;
        defaultArgNamespace_[symbol] = namespaceStack_;
    }
    return functions_.back();
}

// **A body cannot be written where the call is**, so every specialization is
// recorded and the definitions replayed afterwards, to a fixed point. And whether
// anything under this key was chosen by a call, which gates every body it holds.
bool Parser::memberIsUsed(const std::string &key) const {
    const std::vector<std::size_t> *set = overloadsOf(key);
    for (std::size_t k = 0; set != nullptr && k < set->size(); k++)
        if (functions_[(*set)[k]].used) return true;
    return false;
}

void Parser::instantiatePending() {
    for (bool again = true; again; ) {
        again = false;
        for (std::size_t i = 0; i < specializations_.size(); i++) {
            if (specializations_[i].emitted) continue;
            // **Made where it was asked for, defined only where it was chosen** -
            // deduction instantiates a candidate before it can rank one, and an
            // ordinary function may win. A body skipped now may be wanted later.
            std::vector<PendingBody> now;
            std::vector<std::size_t> outsideNow;
            if (specializations_[i].isClass) {
                std::vector<PendingBody> later;
                for (std::size_t b = 0; b < specializations_[i].bodies.size(); b++) {
                    const PendingBody &body = specializations_[i].bodies[b];
                    // **This overload, not this name.** Every constructor of
                    // a class shares one key, so asking the key replays them
                    // all as soon as any is called.
                    const bool wanted =
                        body.which != PendingBody::npos() &&
                        body.which < functions_.size()
                            ? functions_[body.which].used
                            : memberIsUsed(body.key);
                    (wanted ? now : later).push_back(body);
                }
                specializations_[i].bodies = later;

                // **Looked up fresh, because the list can still be growing.** An
                // out-of-line definition may be written further down the file than
                // the use that asked for the class.
                const TemplateDecl &d = templates_[specializations_[i].name];
                std::vector<bool> &done = specializations_[i].outsideDone;
                done.resize(d.outOfLine.size(), false);
                for (std::size_t k = 0; k < d.outOfLine.size(); k++) {
                    if (done[k]) continue;
                    // A static data member has no function to be "used", so it
                    // is replayed with the specialization rather than on a call.
                    if (!d.outOfLine[k].isData &&
                        !memberIsUsed(specializations_[i].key + "::" +
                                      d.outOfLine[k].member)) continue;
                    done[k] = true;
                    outsideNow.push_back(k);
                }
                if (now.empty() && outsideNow.empty()) continue;
            } else {
                const std::vector<std::size_t> *had =
                    overloadsOf(specializations_[i].key);
                if (had == nullptr || !functions_[(*had)[0]].used) continue;
                specializations_[i].emitted = true;
            }
            again = true;

            // Copied, not held by reference: replaying may append to the
            // vector and move it.
            const Specialization sp = specializations_[i];
            TemplateDecl decl = templates_[sp.name];

            std::vector<Shadow> undo;
            bindTemplateParameters(sp.params, sp.binding, sp.values, sp.packs, &undo);
            const std::string wasKey = instantiationKey_;
            const std::string wasOf = instantiationOf_;
            instantiationKey_ = sp.isClass ? std::string() : sp.key;
            instantiationOf_ = sp.isClass ? std::string() : sp.name;

            const std::size_t resume = at_;
            // **An instantiated definition has vague linkage** - [basic.link] and [temp.spec]: the
            // same specialization may be produced by every translation unit that uses it, and the
            // linker folds the copies rather than rejecting them.
            const bool wasReplayingInline = replayingInline_;
            replayingInline_ = true;
            if (sp.isClass) {
                // Its member functions, held here; inlineOwner_ gives the scope.
                replayInlineBodies(now);
                // And the ones written outside it, which need no owner: the
                // tokens say `Box<T>::get`, so with T bound the ordinary
                // member-definition path reads the qualifier itself.
                for (std::size_t k = 0; k < outsideNow.size(); k++) {
                    at_ = templates_[sp.name].outOfLine[outsideNow[k]].start;
                    topLevel(*current_);
                }
            } else {
                at_ = sp.start;
                topLevel(*current_);
            }
            replayingInline_ = wasReplayingInline;
            at_ = resume;

            instantiationKey_ = wasKey;
            instantiationOf_ = wasOf;
            unbindTemplateParameters(undo);
        }
    }
}

bool Parser::mentionsDeduced(const Type *t) {
    if (t == nullptr) return false;
    if (t->kind() == Kind::Deduced) return true;
    if (t->unqualified() != t) return mentionsDeduced(t->unqualified());
    if (t->isPointer() || t->isArray()) return mentionsDeduced(t->pointee());
    if (t->isReference()) return mentionsDeduced(t->referent());
    return false;
}

// The declared type with `auto` replaced, keeping everything written around
// it: `const auto &` deduced as int is `const int &`.
const Type *Parser::substituteDeduced(const Type *t, const Type *with) {
    if (t->kind() == Kind::Deduced) return with;
    if (t->unqualified() != t)
        return types_.withConst(substituteDeduced(t->unqualified(), with));
    if (t->isPointer()) return types_.pointerTo(substituteDeduced(t->pointee(), with));
    if (t->isReference()) return types_.referenceTo(substituteDeduced(t->referent(), with));
    if (t->isArray())
        return types_.arrayOf(substituteDeduced(t->pointee(), with), t->length());
    return t;
}

// **The initialiser is read twice: once to learn its type, once to build it.** The
// tokens are put back in between, so the ordinary declaration path sees exactly
// what it would have seen with the type written out.
const Type *Parser::deduceAuto(const Type *declared, const std::string &name,
                               std::size_t pos) {
    // `auto x{...}` has an initialiser and is refused for the reason below,
    // not for having none - the braces are what cannot be deduced from,
    // however they were introduced.
    const bool paren = peek().is("(") && atParenInitialiser();
    if (!peek().is("=") && !peek().is("{") && !paren)
        src_.fail(pos, "'" + name + "' is declared 'auto' and has no "
                       "initialiser, so there is nothing to deduce its type "
                       "from");

    const std::size_t resume = at_;
    if (paren) at_++;
    else consume("=");
    if (peek().is("{"))
        src_.fail(peek().pos, "'auto' from a braced initialiser is not "
                              "supported yet - it deduces an "
                              "initializer_list, which this compiler has no "
                              "library for");
    // **Read for its type and rewound**, so this reading built nothing: the
    // real one happens below, from the same tokens.
    const Type *from = nullptr;
    {
        Discarded held(this);
        ExprPtr init = assign();
        from = init->type();
    }
    at_ = resume;

    return deduceAutoFrom(declared, from, name, pos);
}

const Type *Parser::deduceAutoFrom(const Type *declared, const Type *from,
                                   const std::string &name, std::size_t pos) {
    std::vector<const Type *> binding(1, static_cast<const Type *>(nullptr));
    std::string why;
    if (!deduceOne(declared, from, &binding, nullptr, &why) || binding[0] == nullptr)
        src_.fail(pos, "'" + name + "' is declared '" + declared->describe() +
                       "' and its initialiser is '" + from->describe() +
                       "', which does not fit: " + why);
    // **What `auto` itself came out as**, which is not the declarator's type:
    // `auto *p = &i` deduces `int` and declares `int *`.
    lastDeducedAuto_ = binding[0];
    return substituteDeduced(declared, binding[0]);
}

// **What a parameter sees of an argument.** [temp.deduct.call]: an array becomes a
// pointer to its first element, a function a pointer to itself, and the top-level
// qualifier goes - which is also just what passing something does.
const Type *Parser::decayedType(const Type *a) const {
    if (a->isReference()) a = a->referent();
    if (a->isArray()) return types_.pointerTo(a->pointee());
    if (a->isFunction()) return types_.pointerTo(a);
    return a->unqualified();
}

// One parameter of the pattern against one argument's type. The pattern still has
// Kind::TemplateParam in it, so "does this position deduce anything" is a question
// about the type and not about a table: one reached here binds.
bool Parser::deduceOne(const Type *pattern, const Type *arg,
                       std::vector<const Type *> *binding,
                       std::vector<long long> *values,
                       std::string *why) const {
    // **A reference parameter looks *through* itself and keeps the argument's
    // qualifier; everything else decays.** `const T &` binding an `int`
    // deduces T as int, and the const on the parameter is not part of T.
    if (pattern->isReference()) {
        pattern = pattern->referent();
        if (arg->isReference()) arg = arg->referent();
        if (pattern->unqualified() != pattern) {
            pattern = pattern->unqualified();
            arg = arg->unqualified();
        }
    } else {
        arg = decayedType(arg);
        if (pattern->unqualified() != pattern) pattern = pattern->unqualified();
    }

    // Kind::Deduced is `auto`, and it is parameter zero of a deduction with
    // one parameter - which is what [dcl.spec.auto] says it is.
    if (pattern->kind() == Kind::TemplateParam ||
        pattern->kind() == Kind::Deduced) {
        const std::size_t i = pattern->kind() == Kind::Deduced
                                  ? 0
                                  : static_cast<std::size_t>(pattern->length());
        const Type *deduced = arg->unqualified();
        if ((*binding)[i] == nullptr) { (*binding)[i] = deduced; return true; }
        if ((*binding)[i] != deduced) {
            *why = "it is '" + (*binding)[i]->describe() + "' in one argument "
                   "and '" + deduced->describe() + "' in another";
            return false;
        }
        return true;
    }

    // **`Holder<T>` against `Holder<int>`.** The two have to be the same
    // template before their arguments mean anything - `Holder<T>` deduces
    // nothing from a `Box<int>`.
    if (pattern->isSpecialization()) {
        if (!arg->isSpecialization() ||
            arg->templateName() != pattern->templateName() ||
            arg->templateArgs().size() != pattern->templateArgs().size()) {
            *why = "'" + arg->describe() + "' is not a '" +
                   pattern->templateName() + "'";
            return false;
        }
        for (std::size_t i = 0; i < pattern->templateArgs().size(); i++) {
            const TemplateArg &p = pattern->templateArgs()[i];
            const TemplateArg &a = arg->templateArgs()[i];
            // **A non-type argument: `V<N>` against `V<3>` gives N=3.** The
            // pattern carries N as a parameter reference; the argument carries a
            // value, and the value is what N is deduced to be.
            if (p.isParam) {
                if (a.isType || a.isParam) {
                    *why = "'" + arg->describe() + "' does not give a value for a "
                           "non-type parameter here";
                    return false;
                }
                const std::size_t j = static_cast<std::size_t>(p.paramIndex);
                if (j < binding->size()) {
                    if ((*binding)[j] != nullptr && values != nullptr &&
                        (*values)[j] != a.value) {
                        *why = "a non-type parameter is deduced two different "
                               "values";
                        return false;
                    }
                    (*binding)[j] = a.type;
                    if (values != nullptr) (*values)[j] = a.value;
                }
                continue;
            }
            if (!p.isType || !a.isType) continue;
            if (!deduceOne(p.type, a.type, binding, values, why)) return false;
        }
        return true;
    }

    if (pattern->isPointer() && arg->isPointer())
        return deduceOne(pattern->pointee(), arg->pointee(), binding, values, why);
    if (pattern->isArray() && arg->isArray())
        return deduceOne(pattern->pointee(), arg->pointee(), binding, values, why);

    // Nothing to deduce here. A parameter written out in full does not have to match
    // exactly - an ordinary conversion may still get the argument there - so this is
    // not where a mismatch is reported; overload resolution ranks it after.
    return true;
}

// The whole call. Answers false with a reason rather than failing, because a
// name may be both a template and an ordinary function: deduction not
// working is then not an error, it is one fewer candidate.
bool Parser::deduceTemplateArguments(const TemplateDecl &decl,
                                     const std::vector<const Type *> &argTypes,
                                     std::vector<const Type *> *binding,
                                     std::vector<long long> *values,
                                     std::vector<std::vector<const Type *> > *packs,
                                     std::string *why) {
    packs->assign(decl.params.size(), std::vector<const Type *>());
    values->assign(decl.params.size(), 0);

    std::vector<const Type *> pattern(decl.params.size());
    for (std::size_t i = 0; i < decl.params.size(); i++)
        pattern[i] = types_.templateParam(static_cast<int>(i));
    const std::vector<long long> none(decl.params.size(), 0);
    std::string ignored;
    const Type *fn = readTemplateDeclaration(decl, pattern, none, &ignored);

    // **A trailing pack takes every argument the written parameters leave.** It is
    // the last parameter by construction, so "the rest" needs no searching - and it
    // may be none, which is why this is a `<` and not a `!=` on the count.
    const bool hasPack = !decl.params.empty() && decl.params.back().isPack;
    const std::size_t fixed = hasPack ? fn->params().size() - 1
                                      : fn->params().size();
    // **A parameter with a default needs no argument.** readTemplateDeclaration above has just read
    // the pattern's parameter list, so pendingDefaults_ says which of them have one - and they are
    // a suffix, [dcl.fct.default]/4.
    std::size_t least = fixed;
    for (std::size_t i = 0; i < pendingDefaults_.size() && i < fixed; i++)
        if (pendingDefaults_[i] != 0) { least = i; break; }
    if (hasPack ? argTypes.size() < fixed
                : (argTypes.size() < least || argTypes.size() > fixed)) {
        *why = "it takes " +
               std::string(hasPack || least < fixed ? "at least " : "") +
               std::to_string(hasPack ? fixed : least) +
               " argument(s) and this call gives " +
               std::to_string(argTypes.size());
        return false;
    }
    // Only the arguments there are can be deduced from; the rest are the
    // defaults, filled in by applyDefaults once a candidate is chosen.
    const std::size_t deduceFrom = argTypes.size() < fixed ? argTypes.size()
                                                           : fixed;

    binding->assign(decl.params.size(), nullptr);
    for (std::size_t i = 0; i < deduceFrom; i++)
        if (!deduceOne(fn->params()[i], argTypes[i], binding, values, why)) {
            *why = "'" + decl.params[i < decl.params.size() ? i : 0].name +
                   "' cannot be worked out from this call: " + *why;
            return false;
        }
    if (hasPack) {
        std::vector<const Type *> members;
        for (std::size_t i = fixed; i < argTypes.size(); i++)
            members.push_back(decayedType(argTypes[i]));
        (*packs)[decl.params.size() - 1] = members;
    }
    for (std::size_t i = 0; i < binding->size(); i++) {
        if (decl.params[i].isPack) continue;
        if ((*binding)[i] == nullptr) {
            *why = "'" + decl.params[i].name + "' appears in no parameter, so "
                   "there is nothing in the call to work it out from - write "
                   "the arguments out";
            return false;
        }
    }
    return true;
}

// Whether parameter `i` appears anywhere in a pattern. One a specialization never
// mentions could not be worked out from any argument list, so it could never be
// chosen - worth refusing where it is written rather than leaving it silent.
static bool mentionsParam(const Type *t, std::size_t i) {
    if (t == nullptr) return false;
    if (t->unqualified() != t) return mentionsParam(t->unqualified(), i);
    if (t->kind() == Kind::TemplateParam)
        return static_cast<std::size_t>(t->length()) == i;
    if (t->isPointer() || t->isArray()) return mentionsParam(t->pointee(), i);
    if (t->isReference()) return mentionsParam(t->referent(), i);
    if (t->isSpecialization()) {
        for (std::size_t k = 0; k < t->templateArgs().size(); k++)
            if (t->templateArgs()[k].isType &&
                mentionsParam(t->templateArgs()[k].type, i)) return true;
        return false;
    }
    return false;
}

Parser::Trial::Trial(Parser *parser)
    : p(parser), at(parser->at_), classes(parser->classStack_.size()),
      pattern(parser->patternOnly_), split(parser->angleSplit_) {
    p->src_.beginTrial();
}

Parser::Trial::~Trial() {
    p->src_.endTrial();
    p->at_ = at;
    p->classStack_.resize(classes);
    p->patternOnly_ = pattern;
    p->angleSplit_ = split;
}

// [temp.deduct.type]. A pattern that is a pointer matches a pointer and
// nothing else - there is no conversion here for a mismatch to be forgiven
// by, which is what makes this stricter than deduction from a call.
bool Parser::matchPattern(const Type *pattern, const Type *arg,
                          std::vector<const Type *> *binding,
                          std::string *why) const {
    // **The qualifier is asked about before anything else, and both sides must
    // agree.** `Box<const T>` matches `Box<const int>` and not `Box<int>`; `Box<T>`
    // matches both, binding T to the qualified type where there is one.
    if (pattern->unqualified() != pattern) {
        if (arg->unqualified() == arg) {
            *why = "'" + arg->describe() + "' is not const";
            return false;
        }
        return matchPattern(pattern->unqualified(), arg->unqualified(),
                            binding, why);
    }

    if (pattern->kind() == Kind::TemplateParam) {
        const std::size_t i = static_cast<std::size_t>(pattern->length());
        if ((*binding)[i] == nullptr) { (*binding)[i] = arg; return true; }
        if ((*binding)[i] != arg) {
            *why = "it is '" + (*binding)[i]->describe() + "' in one place and '" +
                   arg->describe() + "' in another";
            return false;
        }
        return true;
    }

    if (pattern->isPointer())
        return arg->isPointer() &&
               matchPattern(pattern->pointee(), arg->pointee(), binding, why);
    if (pattern->isReference())
        return arg->isReference() &&
               matchPattern(pattern->referent(), arg->referent(), binding, why);
    if (pattern->isArray())
        return arg->isArray() && pattern->length() == arg->length() &&
               matchPattern(pattern->pointee(), arg->pointee(), binding, why);

    if (pattern->isSpecialization()) {
        if (!arg->isSpecialization() ||
            arg->templateName() != pattern->templateName() ||
            arg->templateArgs().size() != pattern->templateArgs().size())
            return false;
        for (std::size_t i = 0; i < pattern->templateArgs().size(); i++) {
            const TemplateArg &p = pattern->templateArgs()[i];
            const TemplateArg &a = arg->templateArgs()[i];
            if (p.isType != a.isType) return false;
            if (!p.isType) {
                if (p.value != a.value) return false;
                continue;
            }
            if (!matchPattern(p.type, a.type, binding, why)) return false;
        }
        return true;
    }

    if (pattern != arg) {
        *why = "'" + arg->describe() + "' is not '" + pattern->describe() + "'";
        return false;
    }
    return true;
}

// `Box<T *>` - read with this specialization's own parameters bound to
// themselves, so what comes out is a pattern rather than a type.
void Parser::partialArguments(TemplateDecl::Partial *ps,
                              const std::vector<TemplateParam> &primary) {
    const std::size_t count = primary.size();
    // **A variadic primary is not written a fixed number of arguments**: a pack
    // stands for a list, so the closing angle says where the pattern stops and not
    // the parameter count - which gave "more arguments than parameters" before.
    const bool variadic = !primary.empty() && primary.back().isPack;
    expect("<");
    const bool wasInArgs = inTemplateArgs_;
    const bool wasPattern = patternOnly_;
    inTemplateArgs_ = true;
    patternOnly_ = true;

    std::vector<Shadow> undo;
    std::vector<const Type *> binding(ps->params.size());
    std::vector<long long> values(ps->params.size(), 1);
    for (std::size_t i = 0; i < ps->params.size(); i++)
        binding[i] = ps->params[i].type == nullptr
                         ? types_.templateParam(static_cast<int>(i))
                         : ps->params[i].type;
    bindTemplateParameters(ps->params, binding, values,
                           std::vector<std::vector<const Type *> >(), &undo);

    for (std::size_t i = 0; variadic ? !atClosingAngle() : i < count; i++) {
        if (i > 0) expect(",");
        TemplateDecl::Partial::Arg a;

        // `R...` - this specialization's own pack, standing for everything
        // the fixed arguments before it did not take.
        if (peek().kind == TokenKind::Ident && peekAt(1).is("...")) {
            std::size_t k = ps->params.size();
            for (std::size_t j = 0; j < ps->params.size(); j++)
                if (ps->params[j].isPack && ps->params[j].name == peek().text)
                    k = j;
            if (k < ps->params.size()) {
                a.isType = true;
                a.isPackExpansion = true;
                a.param = k;
                at_ += 2;
                ps->args.push_back(a);
                continue;
            }
        }
        // **A non-type argument that is one of our own parameters is the only
        // shape of one that deduces**, so it is recognised by its tokens
        // before it can be folded into the value it was bound to.
        std::size_t which = ps->params.size();
        if (peek().kind == TokenKind::Ident)
            for (std::size_t k = 0; k < ps->params.size(); k++)
                if (ps->params[k].type != nullptr &&
                    ps->params[k].name == peek().text) which = k;
        if (which < ps->params.size() &&
            (peekAt(1).is(",") || peekAt(1).is(">") || peekAt(1).is(">>"))) {
            a.isType = false;
            a.isParam = true;
            a.param = which;
            at_++;
        } else if (atTypeName()) {
            StorageClass sc;
            Qualifiers quals;
            const Type *base = specifiers(&sc, &quals);
            Declared d = declarator(base, true);
            a.isType = true;
            a.type = d.type;
        } else {
            a.isType = false;
            a.value = constantExpression("a template argument");
        }
        ps->args.push_back(a);
    }
    if (!atClosingAngle())
        src_.fail(peek().pos, "this specialization gives more arguments than "
                              "the template has parameters");
    takeClosingAngle();
    // A pack expansion may only be last: everything after it could never be
    // told apart from a member of it. Same rule, and the same reason, as a
    // pack having to be the last *parameter*.
    for (std::size_t i = 0; i + 1 < ps->args.size(); i++)
        if (ps->args[i].isPackExpansion)
            src_.fail(ps->params.empty() ? 0 : ps->params[0].pos,
                      "a pack expansion has to be the last argument of a "
                      "specialization - anything after it could never be told "
                      "apart from one of its members");

    unbindTemplateParameters(undo);
    inTemplateArgs_ = wasInArgs;
    patternOnly_ = wasPattern;
}

// [temp.class.order], asked the standard's own way: A is at least as specialized as
// B when B's pattern matches A's. A's parameters stand as opaque types while that
// happens, which is exactly what Kind::TemplateParam already is.
bool Parser::atLeastAsSpecialized(const TemplateDecl::Partial &a,
                                  const TemplateDecl::Partial &b) const {
    std::vector<const Type *> binding(b.params.size());
    std::string why;
    for (std::size_t i = 0; i < a.args.size() && i < b.args.size(); i++) {
        if (a.args[i].isType != b.args[i].isType) return false;
        if (!a.args[i].isType) {
            if (b.args[i].isParam) continue;      // a parameter takes anything
            if (a.args[i].isParam) return false;
            if (a.args[i].value != b.args[i].value) return false;
            continue;
        }
        if (!matchPattern(b.args[i].type, a.args[i].type, &binding, &why))
            return false;
    }
    return true;
}

bool Parser::moreSpecialized(const TemplateDecl::Partial &a,
                             const TemplateDecl::Partial &b) const {
    return atLeastAsSpecialized(a, b) && !atLeastAsSpecialized(b, a);
}

// Which partial specialization these arguments ask for.
std::size_t Parser::choosePartial(const TemplateDecl &decl,
                                  const std::vector<TemplateArg> &args,
                                  std::vector<const Type *> *binding,
                                  std::vector<long long> *values,
                                  std::vector<std::vector<const Type *> > *packs,
                                  std::size_t pos) {
    std::vector<std::size_t> fits;
    std::vector<std::vector<const Type *> > bindings;
    std::vector<std::vector<long long> > valueSets;
    std::vector<std::vector<std::vector<const Type *> > > packSets;

    // **The arguments arrive as one pack when the primary is variadic.**
    // `L<int, char>` against `template <class... Ts>` is a single TemplateArg
    // holding both, so the list is flattened here and every pattern matched on it.
    std::vector<TemplateArg> flat;
    for (std::size_t i = 0; i < args.size(); i++) {
        if (!args[i].isPack) { flat.push_back(args[i]); continue; }
        for (std::size_t k = 0; k < args[i].pack.size(); k++) {
            TemplateArg one;
            one.isType = true;
            one.type = args[i].pack[k];
            flat.push_back(one);
        }
    }

    for (std::size_t p = 0; p < decl.partials.size(); p++) {
        const TemplateDecl::Partial &ps = decl.partials[p];

        // A trailing `R...` takes everything the fixed arguments leave, so
        // the arity it demands is a minimum rather than an equality.
        const bool takesRest = !ps.args.empty() &&
                               ps.args.back().isPackExpansion;
        const std::size_t fixed = takesRest ? ps.args.size() - 1
                                            : ps.args.size();
        if (takesRest ? flat.size() < fixed : ps.args.size() != flat.size())
            continue;

        std::vector<const Type *> b(ps.params.size());
        std::vector<long long> v(ps.params.size(), 0);
        std::vector<std::vector<const Type *> > pk(ps.params.size());
        std::string why;
        bool ok = true;
        for (std::size_t i = 0; i < fixed && ok; i++) {
            const TemplateDecl::Partial::Arg &a = ps.args[i];
            if (a.isType != flat[i].isType) { ok = false; break; }
            if (!a.isType) {
                if (a.isParam) v[a.param] = flat[i].value;
                else if (a.value != flat[i].value) ok = false;
                continue;
            }
            if (!matchPattern(a.type, flat[i].type, &b, &why)) ok = false;
        }
        if (ok && takesRest) {
            const TemplateDecl::Partial::Arg &tail = ps.args.back();
            for (std::size_t i = fixed; i < flat.size() && ok; i++) {
                if (!flat[i].isType) { ok = false; break; }
                pk[tail.param].push_back(flat[i].type);
            }
        }
        // A pack parameter binds through `pk` and never through `b`, so it is
        // not the unbound-parameter fault this is looking for.
        for (std::size_t i = 0; ok && i < ps.params.size(); i++)
            if (ps.params[i].type == nullptr && !ps.params[i].isPack &&
                b[i] == nullptr) ok = false;
        if (!ok) continue;
        fits.push_back(p);
        bindings.push_back(b);
        valueSets.push_back(v);
        packSets.push_back(pk);
    }

    if (fits.empty()) return static_cast<std::size_t>(-1);

    // **One has to beat every other, and "not beaten" is not the same as "beats".**
    // `P<A, int>` and `P<int, B>` given `P<int, int>`: neither matches the other, so
    // the program is ambiguous rather than settled by whichever came first.
    std::size_t best = 0;
    for (std::size_t k = 1; k < fits.size(); k++)
        if (moreSpecialized(decl.partials[fits[k]], decl.partials[fits[best]]))
            best = k;
    for (std::size_t k = 0; k < fits.size(); k++)
        if (k != best &&
            !moreSpecialized(decl.partials[fits[best]], decl.partials[fits[k]]))
            src_.fail(pos, "'" + decl.name + "' has two partial "
                           "specializations that fit these arguments and "
                           "neither is more specialized than the other");

    *binding = bindings[best];
    *values = valueSets[best];
    if (packs != nullptr) *packs = packSets[best];
    return fits[best];
}

// `Box<int, 3>` where a type was expected - rung 5.4. The class is made by replaying
// `struct Box { ... };` with the arguments bound, and all the class path had to be
// told is what tag to take: nested classes had made tag() an arbitrary string.
const Type *Parser::instantiateClass(const TemplateDecl &decl, std::size_t pos) {
    std::vector<const Type *> binding;
    std::vector<long long> values;
    std::vector<TemplateArg> args;
    std::vector<std::vector<const Type *> > packs;
    templateArguments(decl, &binding, &values, &args, &packs);

    const std::string tag = specializationKey(decl.name, args);

    // Reading a pattern, not building a class. `Holder<T>` cannot be laid out
    // - T has no size - and neither the mangler nor deduction wants it laid
    // out: both read only the template's name and its argument list.
    if (patternOnly_) {
        Type *shallow = types_.structType(Kind::Struct, tag);
        if (!shallow->isSpecialization()) {
            shallow->setSpecialization(decl.name, args);
            shallow->setTemplateNamespace(decl.ns);
        }
        // Registered so that `Box<T>::get` reads: the declarator's qualified
        // path looks the class up by name, and this is the only name it has.
        // The tag holds a `$` and so cannot collide with anything written.
        declareTypeName(tag, shallow);
        return shallow;
    }

    if (const Type *had = findTypedef(tag)) return had;

    // **A partial specialization is chosen before anything is replayed**, and what
    // it changes is which tokens get replayed and with which parameters bound. The
    // tag does not change, which keeps the mangling and every lookup the same.
    std::vector<const Type *> useBinding = binding;
    std::vector<long long> useValues = values;
    std::vector<TemplateParam> useParams = decl.params;
    std::vector<std::vector<const Type *> > usePacks;
    const std::size_t which = choosePartial(decl, args, &useBinding, &useValues,
                                            &usePacks, pos);
    const bool partial = which != static_cast<std::size_t>(-1);
    if (partial) useParams = decl.partials[which].params;

    // **Asked after the partial is chosen, not before.**
    if (!partial && !decl.defined) {
        Type *shallow = types_.structType(Kind::Struct, tag);
        if (!shallow->isSpecialization()) {
            shallow->setSpecialization(decl.name, args);
            shallow->setTemplateNamespace(decl.ns);
            shallow->noteClassKey(decl.classKey);
        }
        return shallow;
    }

    const std::size_t resume = at_;
    std::vector<Shadow> undo;
    // **A chosen partial binds its own pack, not the primary's.** `L<T, R...>` on
    // `L<int, char, long>` leaves R holding {char, long}, which is what the replayed
    // body has to see - the primary's Ts is not in scope at all.
    bindTemplateParameters(useParams, useBinding, useValues,
                           partial ? usePacks : packs, &undo);

    at_ = partial ? decl.partials[which].bodyAt : decl.afterParams;
    // **A specialization is not a member of whatever class asked for it.**
    std::vector<const Type *> outerClasses;
    outerClasses.swap(classStack_);
    const Type *outerCurrent = currentClass_;
    currentClass_ = nullptr;
    const std::string outerInline = inlineOwner_;
    inlineOwner_.clear();
    // **The `>>` mark is one slot and this parse can spend it.**
    const std::size_t outerAngle = angleSplit_;

    classInstantiationTag_ = tag;
    if (partial) classInstantiationOf_ = decl.name;
    instantiatingArgs_ = args;
    instantiatingNamespace_ = decl.ns;
    instantiatingParams_ = useParams;
    instantiatingBinding_ = useBinding;
    instantiatingValues_ = useValues;
    heldForSpecialization_.clear();
    const bool wasDeferring = deferSpecializationBodies_;
    deferSpecializationBodies_ = true;
    StorageClass sc;
    Qualifiers quals;
    const Type *made = partial
        ? structOrUnionSpecifier(Kind::Struct, false)
        : specifiers(&sc, &quals);
    angleSplit_ = outerAngle;
    classInstantiationTag_.clear();
    classInstantiationOf_.clear();
    instantiatingArgs_.clear();
    classStack_.swap(outerClasses);
    currentClass_ = outerCurrent;
    inlineOwner_ = outerInline;
    deferSpecializationBodies_ = wasDeferring;
    std::vector<PendingBody> bodies;
    bodies.swap(heldForSpecialization_);

    unbindTemplateParameters(undo);
    at_ = resume;

    if (!made->isStructOrUnion())
        src_.fail(pos, "'" + decl.name + "' is not a class template");
    declareTypeName(tag, made);

    Specialization sp;
    sp.key = tag;
    sp.name = decl.name;
    sp.params = useParams;
    sp.binding = useBinding;
    sp.values = useValues;
    // **A partial's pack has to be recorded too.** Held bodies are replayed later
    // from this record, and one saying `Tuple<Rest...>` needs Rest as it was. Left
    // empty, `Tuple<char,long>::tail` returned `Tuple<long> &` and said `Tuple<> &`.
    sp.packs = partial ? usePacks : packs;
    sp.start = decl.afterParams;
    sp.pos = pos;
    sp.isClass = true;
    sp.bodies = bodies;
    specializations_.push_back(sp);
    return made;
}

// A template named in an expression. 5.2 wants the arguments written out:
// deducing them from the call is 5.3, and a class template is 5.4.
ExprPtr Parser::templateCall(Program *program) {
    const std::string name = peek().text;
    const std::size_t pos = peek().pos;
    const TemplateDecl decl = templates_[name];
    if (decl.isClass) {
        // **`vector<int>()` is a temporary, not an instantiation this cannot
        // do.**
        const std::size_t save = at_;
        at_++;
        if (peek().is("<")) {
            const Type *cls = instantiateClass(decl, pos);
            if (cls != nullptr && cls->isStructOrUnion() && peek().is("(")) {
                at_++;
                return classTemporary(cls, pos);
            }
            // **`CNeeds<(N == 3)>::check()` - a static member through a
            // template-id.**
            if (cls != nullptr && cls->isStructOrUnion() && peek().is("::")) {
                at_++;
                return templateIdMember(cls, pos);
            }
        }
        // **The injected class name.** Inside V<T>'s own members `V` means
        // this specialization, not the template - [temp.local] - so `V(x)` is
        // a temporary of it and needs no argument list.
        if (peek().is("(")) {
            if (const Type *self = findTypedef(name))
                if (self->isStructOrUnion()) {
                    at_++;
                    return classTemporary(self, pos);
                }
        }
        at_ = save;
        refuseTemplateId();
    }
    at_++;

    // **No argument list, so they come from the call.** The arguments are parsed
    // before anything can be deduced from them, the other way round from the
    // written case - and the order overload resolution wants.
    if (!peek().is("<")) {
        if (!peek().is("("))
            src_.fail(pos, "'" + name + "' is a function template, and naming "
                           "one without calling it is not supported yet");
        at_++;
        std::vector<ExprPtr> callArgs;
        parseArguments(callArgs);

        std::vector<const Type *> argTypes;
        for (std::size_t i = 0; i < callArgs.size(); i++)
            argTypes.push_back(callArgs[i]->type());

        // **Every function template of this name is a candidate.**
        instantiateViableTemplates(name, argTypes, pos);

        // Nothing deduced and no ordinary function - so say why the template
        // failed, which beats "not declared". With more than one overload the
        // first is representative enough for the message.
        if (overloadsOf(name) == nullptr) {
            std::vector<const Type *> b;
            std::vector<long long> v;
            std::vector<std::vector<const Type *> > p;
            std::string why;
            deduceTemplateArguments(decl, argTypes, &b, &v, &p, &why);
            // Deduction can succeed and the instantiation still fail, and then
            // there is no reason to print - so say that rather than end the
            // sentence on "and".
            if (why.empty())
                why = "no specialization of it could be made for these "
                      "arguments";
            src_.fail(pos, "'" + name + "' is a function template and " + why);
        }

        const Signature sig = resolveOverload(name, callArgs, pos);
        // **A specialization's defaults, as every ordinary call reads them.**
        applyDefaults(sig, callArgs, pos);
        return completeCall(sig.name, sig.symbol, nullptr, sig.returns,
                            sig.params, sig.variadic, pos, std::move(callArgs));
    }

    std::vector<const Type *> binding;
    std::vector<long long> values;
    std::vector<TemplateArg> args;
    std::vector<std::vector<const Type *> > packs;
    templateArguments(decl, &binding, &values, &args, &packs);

    // **A copy, not a reference**: `sig.params` is handed to `completeCall`,
    // and parsing the arguments can declare a function and move `functions_`.
    const Signature sig = instantiate(decl, binding, values, args, pos, packs);
    if (!peek().is("("))
        src_.fail(peek().pos, "'" + name + "' is a function template, and "
                              "naming one without calling it is not supported "
                              "yet");
    at_++;
    (void)program;
    std::vector<ExprPtr> callArgs;
    parseArguments(callArgs);

    // Written out rather than deduced, so no ranking chose it and nothing else
    // will mark it - a specialization is defined only where it was chosen.
    for (std::size_t i = 0; i < functions_.size(); i++)
        if (functions_[i].symbol == sig.symbol) { functions_[i].used = true; break; }
    // The same defaults, on the path where the arguments were written out.
    applyDefaults(sig, callArgs, pos);
    return completeCall(sig.name, sig.symbol, nullptr, sig.returns, sig.params,
                        sig.variadic, pos, std::move(callArgs));
}

void Parser::refuseTemplateId() {
    const std::string name = peek().text;
    const std::size_t pos = peek().pos;
    at_++;
    if (peek().is("<")) skipTemplateArguments();
    // **`A<int>::n` is not an instantiation this cannot do.** The type is made
    // perfectly well in a declaration; what is missing is reading a template-id as
    // the qualifier of a name. The typedef it names is the whole workaround.
    if (peek().is("::"))
        src_.fail(pos, "'" + name + "<...>::' - naming a member through a "
                       "class template's argument list is not supported yet; "
                       "the type itself is made, so 'typedef " + name +
                       "<...> Name;' and then 'Name::' reaches the member");
    src_.fail(pos, "'" + name + "' is a " +
                   (templates_[name].isClass ? "class" : "function") +
                   " template, and instantiating one is not supported yet");
}


// **A member function template call, v.head<3>().**
ExprPtr Parser::memberTemplateCall(ExprPtr object, const Type *obj,
                                   const std::string &name, std::size_t pos) {
    const Type *plain = obj->unqualified();
    const TemplateDecl mt = memberTemplates_[plain->tag() + "::" + name];

    std::vector<const Type *> binding;
    std::vector<long long> values;
    std::vector<TemplateArg> args;
    std::vector<std::vector<const Type *> > packs;
    std::vector<ExprPtr> deducedArgs;
    bool deduced = false;

    if (peek().is("<")) {
        templateArguments(mt, &binding, &values, &args, &packs);
    } else {
        // **No argument list, so they come from the call** - the same order the
        // free-function path uses: the arguments are parsed first and the
        // parameters worked out from their types, [temp.deduct.call].
        expect("(");
        parseArguments(deducedArgs);
        deduced = true;
        std::vector<const Type *> argTypes;
        for (std::size_t i = 0; i < deducedArgs.size(); i++)
            argTypes.push_back(deducedArgs[i]->type());
        std::string why;
        std::vector<Shadow> undo;
        if (!mt.classParams.empty())
            bindTemplateParameters(mt.classParams, mt.classBinding,
                                   mt.classValues,
                                   std::vector<std::vector<const Type *> >(),
                                   &undo);
        const bool ok = deduceTemplateArguments(mt, argTypes, &binding, &values,
                                                &packs, &why);
        unbindTemplateParameters(undo);
        if (!ok)
            src_.fail(pos, "'" + plain->describe() + "::" + name + "' is a "
                           "member function template and " + why);
        for (std::size_t i = 0; i < binding.size(); i++) {
            TemplateArg a;
            if (mt.params[i].type != nullptr) {
                a.isType = false; a.type = mt.params[i].type; a.value = values[i];
            } else if (mt.params[i].isPack) {
                a.isType = true; a.isPack = true; a.pack = packs[i];
            } else {
                a.isType = true; a.type = binding[i];
            }
            args.push_back(a);
        }
    }

    const Signature *sig = instantiateMemberTemplate(mt, binding, values, args, pos);
    if (sig == nullptr)
        src_.fail(pos, "'" + plain->describe() + "::" + name + "' could not be "
                       "instantiated with these template arguments");

    std::vector<ExprPtr> callArgs;
    if (deduced) {
        callArgs = std::move(deducedArgs);
    } else {
        if (!peek().is("("))
            src_.fail(peek().pos, "'" + name + "' is a member function "
                                  "template, and naming one without calling it "
                                  "is not supported yet");
        at_++;
        parseArguments(callArgs);
    }

    // A copy, since applyDefaults and completeCall read it while the arguments
    // can grow functions_ out from under a reference.
    const Signature chosen = *sig;
    applyDefaults(chosen, callArgs, pos);

    if (obj->isConst() && !chosen.constThis)
        src_.fail(pos, "'" + name + "' is not a const member function, and this "
                       "object is const");

    const Type *pointee = chosen.constThis ? types_.withConst(plain) : plain;
    const Type *thisType = types_.pointerTo(pointee);
    ExprPtr addr(new Unary('&', std::move(object)));
    addr->setType(thisType);

    std::vector<const Type *> full;
    full.push_back(thisType);
    for (std::size_t i = 0; i < chosen.params.size(); i++)
        full.push_back(chosen.params[i]);

    std::vector<ExprPtr> all;
    all.push_back(std::move(addr));
    for (std::size_t i = 0; i < callArgs.size(); i++)
        all.push_back(std::move(callArgs[i]));

    return completeCall(name, chosen.symbol, nullptr, chosen.returns, full,
                        chosen.variadic, pos, std::move(all), true);
}

// The specialization these arguments ask for, made if it is new.
const Parser::Signature *Parser::instantiateMemberTemplate(
    const TemplateDecl &mt, const std::vector<const Type *> &binding,
    const std::vector<long long> &values,
    const std::vector<TemplateArg> &args, std::size_t pos) {
    const std::string display = specializationKey(mt.name, args);   // head<3>
    const std::string key = mt.ownerTag + "::" + display;
    if (const std::vector<std::size_t> *had = overloadsOf(key))
        return &functions_[(*had)[0]];
    if (!mt.defined)
        src_.fail(pos, "'" + mt.ownerTag + "::" + mt.name + "' is declared but "
                       "not defined, so there is nothing to instantiate");

    std::vector<Shadow> undo;
    // The class's parameters first (N), then the member's own (M) - two layers,
    // the whole reason a member template records the class's binding.
    if (!mt.classParams.empty())
        bindTemplateParameters(mt.classParams, mt.classBinding, mt.classValues,
                               std::vector<std::vector<const Type *> >(), &undo);
    bindTemplateParameters(mt.params, binding, values,
                           std::vector<std::vector<const Type *> >(), &undo);

    const bool wasInst = memberTemplateInst_;
    const std::string wasOf = memberTemplateOf_;
    const std::string wasName = memberTemplateName_;
    const std::vector<TemplateArg> wasArgs = memberTemplateArgs_;
    memberTemplateInst_ = true;
    memberTemplateOf_ = mt.name;
    memberTemplateName_ = display;
    memberTemplateArgs_ = args;
    memberTemplateAccess_ = mt.memberAccess;

    std::vector<PendingBody> one(1);
    one[0].tag = mt.ownerTag;
    one[0].start = mt.afterParams;
    one[0].local = mt.ownerTag;
    one[0].key = key;
    one[0].which = PendingBody::npos();
    replayInlineBodies(one);

    memberTemplateInst_ = wasInst;
    memberTemplateOf_ = wasOf;
    memberTemplateName_ = wasName;
    memberTemplateArgs_ = wasArgs;
    unbindTemplateParameters(undo);

    const std::vector<std::size_t> *had = overloadsOf(key);
    return had != nullptr ? &functions_[(*had)[0]] : nullptr;
}

// **Function templates as overload-resolution candidates.**
void Parser::instantiateViableTemplates(const std::string &name,
                                        const std::vector<const Type *> &argTypes,
                                        std::size_t pos) {
    std::map<std::string, std::vector<TemplateDecl> >::const_iterator it =
        fnTemplates_.find(name);
    if (it == fnTemplates_.end()) return;
    const std::vector<TemplateDecl> overloads = it->second;
    for (std::size_t k = 0; k < overloads.size(); k++) {
        const TemplateDecl &decl = overloads[k];
        std::vector<const Type *> binding;
        std::vector<long long> values;
        std::vector<std::vector<const Type *> > packs;
        std::string why;
        if (!deduceTemplateArguments(decl, argTypes, &binding, &values, &packs,
                                     &why))
            continue;
        std::vector<TemplateArg> args;
        for (std::size_t i = 0; i < binding.size(); i++) {
            TemplateArg a;
            if (decl.params[i].type != nullptr) {
                a.isType = false; a.type = decl.params[i].type; a.value = values[i];
            } else if (decl.params[i].isPack) {
                a.isType = true; a.isPack = true; a.pack = packs[i];
            } else {
                a.isType = true; a.type = binding[i];
            }
            args.push_back(a);
        }
        try {
            Trial trial(this);
            instantiate(decl, binding, values, args, pos, packs);
        } catch (const SubstitutionFailure &f) {
            // This template does not apply; the others still might - unless
            // what stopped it was a feature this compiler has not built, which
            // is not [temp.deduct]/8's business and must not be swallowed.
            if (f.unsupported) src_.fail(f.pos, f.why);
        }
    }
}

// **A static member reached through a class template-id**, `CNeeds<(N==3)>::
// check()` or `std::numeric_limits<int>::max()`.
ExprPtr Parser::templateIdMember(const Type *cls, std::size_t pos) {
    (void)pos;
    const std::size_t mpos = peek().pos;
    const std::string member = declaredName("a member name");
    const std::string key = cls->tag() + "::" + member;
    if (peek().is("(")) {
        at_++;
        std::vector<ExprPtr> callArgs;
        parseArguments(callArgs);
        if (overloadsOf(key) == nullptr)
            src_.fail(mpos, "'" + cls->describe() + "' has no static "
                            "member function '" + member + "' - only "
                            "a static member is reachable through a "
                            "template-id with no object");
        const Signature &sig = resolveOverload(key, callArgs, mpos);
        applyDefaults(sig, callArgs, mpos);
        if (needsThis(sig))
            src_.fail(mpos, "'" + key + "' is not a static member "
                            "function, so it has to be called on an "
                            "object");
        if (sig.access != Access::Public) {
            const Type *ownerType = findTypedef(sig.owner);
            if (ownerType == nullptr ||
                (!insideAccessOf(ownerType, sig.access) &&
                 !isFriendOf(ownerType))) {
                const char *how = sig.access == Access::Private
                                      ? "private" : "protected";
                src_.fail(mpos, "'" + key + "' is " + how + " in '" +
                                sig.owner + "'");
            }
        }
        return completeCall(key, sig.symbol, nullptr, sig.returns,
                            sig.params, sig.variadic, mpos,
                            std::move(callArgs), false);
    }
    if (const Type::StaticMember *sm = cls->findStaticMember(member))
        return staticMemberRef(cls, *sm, cls->tag(), mpos);
    // **An enumerator of the specialization is reached the same way**, and is
    // a value rather than an object - so there is nothing to take the address
    // of and the number is the whole of it.
    if (const EnumConst *e = enumInClass(cls, member)) {
        ExprPtr n(new Num(e->value));
        n->setType(types_.intType());
        return n;
    }
    src_.fail(mpos, "'" + cls->describe() + "' has no static member or "
                    "enumerator called '" + member + "'");
}

// **A member function template as an overload-resolution candidate.**
void Parser::instantiateViableMemberTemplates(
    const Type *cls, const std::string &name,
    const std::vector<const Type *> &argTypes, std::size_t pos) {
    if (cls == nullptr || cls->tag().empty()) return;
    std::map<std::string, TemplateDecl>::const_iterator it =
        memberTemplates_.find(cls->tag() + "::" + name);
    if (it == memberTemplates_.end()) return;
    const TemplateDecl mt = it->second;

    std::vector<const Type *> binding;
    std::vector<long long> values;
    std::vector<std::vector<const Type *> > packs;
    std::string why;
    std::vector<Shadow> undo;
    if (!mt.classParams.empty())
        bindTemplateParameters(mt.classParams, mt.classBinding, mt.classValues,
                               std::vector<std::vector<const Type *> >(), &undo);
    const bool ok = deduceTemplateArguments(mt, argTypes, &binding, &values,
                                            &packs, &why);
    unbindTemplateParameters(undo);
    if (!ok) return;

    std::vector<TemplateArg> args;
    for (std::size_t i = 0; i < binding.size(); i++) {
        TemplateArg a;
        if (mt.params[i].type != nullptr) {
            a.isType = false; a.type = mt.params[i].type; a.value = values[i];
        } else if (mt.params[i].isPack) {
            a.isType = true; a.isPack = true; a.pack = packs[i];
        } else {
            a.isType = true; a.type = binding[i];
        }
        args.push_back(a);
    }
    try {
        Trial trial(this);
        instantiateMemberTemplate(mt, binding, values, args, pos);
    } catch (const SubstitutionFailure &f) {
        // This one does not apply - unless it was a refusal, which is a fact
        // about the compiler rather than about this candidate.
        if (f.unsupported) src_.fail(f.pos, f.why);
    }
}
