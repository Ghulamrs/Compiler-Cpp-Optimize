// The parser: templates. Template parameters and the scope they are bound in,
// argument deduction, partial ordering and specialisation, and the token replay
// that turns a pattern into an instantiation. Rung 5.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Operator.h"
#include "../Source.h"

#include <climits>
#include <cstdlib>
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
void Parser::templateParameters(std::vector<TemplateParam> &params, bool allowUnnamed) {
    expect("<");
    if (atClosingAngle())
        src_.fail(peek().pos, "'template <>' is an explicit specialization, "
                              "and that is not supported yet");
    for (;;) {
        const std::size_t pos = peek().pos;
        TemplateParam p;
        p.pos = pos;
        // **`template <class> class C`** - [temp.param]/1. Its own list may go unnamed; the
        // keyword after it is `class` in C++11, `typename` there being C++17's (P0522's neighbour N4051).
        if (consume("template")) {
            p.isTemplate = true;
            templateParameters(p.inner, true);
            if (peek().is("typename"))
                src_.fail(peek().pos, "'typename' after a template template parameter's list is "
                                      "C++17, and this compiler is C++11 - write 'class' there");
            if (!consume("class"))
                src_.fail(peek().pos, "expected 'class' after a template template parameter's list");
            if (consume("...")) p.isPack = true;
            p.namePos = peek().pos;
            if (peek().kind == TokenKind::Ident) { p.name = peek().text; at_++; }
        } else if (consume("class") ||
                   // [temp.param]/2: `typename X<T>::type v` - a qualified name after it - is a non-type
                   // parameter of that type, read below; `typename T` alone is a type parameter.
                   (peek().is("typename") && !peekAt(1).is("::") &&
                    !(peekAt(1).kind == TokenKind::Ident && (peekAt(2).is("::") || peekAt(2).is("<"))) &&
                    consume("typename"))) {
            if (consume("...")) p.isPack = true;
            p.namePos = peek().pos;
            if (peek().kind == TokenKind::Ident) { p.name = peek().text; at_++; }
        } else {
            // A non-type parameter is written exactly like a function's, so it is read like one.
            // **`T v` after `class T`**, [temp.param]/4: the earlier type parameters stand for
            // themselves while this one's type is read, as a function template's signature does.
            std::vector<Shadow> undo;
            std::vector<TemplateParam> earlier;
            std::vector<const Type *> stand;
            for (std::size_t k = 0; k < params.size(); k++)
                if (params[k].type == nullptr && !params[k].isPack) {
                    earlier.push_back(params[k]);
                    stand.push_back(types_.templateParam(static_cast<int>(k)));
                }
            bindTemplateParameters(earlier, stand, std::vector<long long>(earlier.size(), 0),
                                   Packs(earlier.size()), &undo);
            // Read as a pattern: `typename EI<sizeof(T) == 1, int>::type` is a dependent member, and no
            // expression over the parameters standing for themselves is made into code here.
            struct Unbind {
                Parser *p; std::vector<Shadow> *u; bool pattern;
                ~Unbind() { p->unbindTemplateParameters(*u); p->patternOnly_ = pattern; }
            } unbind{ this, &undo, patternOnly_ };
            if (!earlier.empty() && peek().is("typename")) patternOnly_ = true;
            p.typeAt = at_;
            p.typeNamespace = namespaceStack_;
            StorageClass sc;
            Qualifiers quals;
            const Type *base = specifiers(&sc, &quals);
            if (sc != StorageNone)
                src_.fail(pos, "a template parameter has no storage class");
            // **`int... Ns`** - [temp.param]/15: a pack of values. The `...` follows the type's
            // own specifiers, so a pointer or a reference in front of it is a shape not built.
            if (consume("...")) {
                p.isPack = true;
            } else {
                for (std::size_t k = 0; peekAt(k).kind != TokenKind::End; k++) {
                    if (peekAt(k).is(",") || peekAt(k).is(">") || peekAt(k).is(">>")) break;
                    if (peekAt(k).is("..."))
                        src_.fail(peekAt(k).pos, "a non-type parameter pack of a pointer, reference or "
                                                 "array type is not supported yet - a pack of an "
                                                 "integral or enumeration type, 'int... Ns', is");
                }
            }
            Declared d = declarator(base, true);
            if (p.isPack && !base->isInteger() && base->kind() != Kind::TemplateParam)
                src_.fail(d.pos, "a non-type parameter pack of type '" + base->describe() + "' is not "
                                 "supported yet - it must be an integral or enumeration type");
            p.name = d.name;
            p.type = d.type;
            p.namePos = d.pos;
            const Type *u = d.type->unqualified();
            if (u->isFloating() || u->isVoid() || (u->isStructOrUnion() && !u->isMemberFunctionPointer() &&
                                                   u->kind() != Kind::DependentMember))
                src_.fail(d.pos, "a non-type template parameter cannot be of type '" + d.type->describe() +
                                 "' - [temp.param]/4 allows an integral, enumeration, pointer, reference, "
                                 "pointer to member or std::nullptr_t type");
            if (d.type->kind() == Kind::TemplateParam)
                p.typeOf = static_cast<int>(d.type->length());
            else if ([&] {
                         for (const Type *t = d.type; t != nullptr; ) {
                             if (t->kind() == Kind::TemplateParam) return true;
                             if (t->unqualified() != t) t = t->unqualified();
                             else if (t->isPointer() || t->isReference()) t = t->pointee();
                             else return false;
                         }
                         return false;
                     }()) {
                if (p.isPack)
                    src_.fail(pos, "a non-type parameter pack whose type is built from an earlier "
                                   "parameter, '" + d.type->describe() + "', is not supported yet - "
                                   "the earlier parameter itself, 'T... Vs', is");
                p.builtOnParam = true;
            }
        }
        // [temp.param]/1 lets a parameter go unnamed: it is bound by its place under a name no source can spell.
        if (p.name.empty()) p.name = "$tp" + std::to_string(params.size());
        // **A default argument, kept as tokens.** [temp.param]/9 lets a
        // parameter carry one, and a use that omits it replays these tokens
        // with the earlier parameters bound - which is how `N = M` sees `M`.
        if (consume("=")) {
            p.defBegin = at_;
            // Inside parentheses `<` is less-than and `>` greater-than ([temp.names]/3 only ends the
            // list at a `>` outside them): `bool = (N < 0)`.
            int depth = 0, parens = 0;
            for (;;) {
                const Token &t = peek();
                if (t.kind == TokenKind::End) break;
                if (depth == 0 && parens == 0 && (t.is(",") || t.is(">") || t.is(">>"))) break;
                // `class = E<(N < 3)>>`: the front half closes the default's own list, the back half this one.
                if (depth == 1 && parens == 0 && t.is(">>")) {
                    angleSplit_ = at_;
                    break;
                }
                if (t.is("(") || t.is("[")) parens++;
                else if (parens > 0 && (t.is(")") || t.is("]"))) parens--;
                else if (parens == 0 && t.is("<")) depth++;
                else if (parens == 0 && depth > 0 && t.is(">")) depth--;
                else if (parens == 0 && depth > 1 && t.is(">>")) depth -= 2;
                at_++;
            }
            p.defEnd = at_;
            if (p.defEnd == p.defBegin)
                src_.fail(peek().pos, "a default template argument has no value");
        }
        for (std::size_t i = 0; i < params.size(); i++)
            if (!p.name.empty() && params[i].name == p.name)
                src_.fail(p.pos, "'" + p.name + "' is declared twice in this "
                                 "template parameter list");
        params.push_back(p);
        if (consume(",")) continue;
        break;
    }
    takeClosingAngle();
    // A template template parameter's own list is a class template's: [temp.param]/11's first sentence.
    if (allowUnnamed) refuseAfterPack(params);
}

// The first parameter written after a pack, or npos.
std::size_t Parser::afterPack(const std::vector<TemplateParam> &params) {
    for (std::size_t i = 0; i + 1 < params.size(); i++)
        if (params[i].isPack) return i + 1;
    return static_cast<std::size_t>(-1);
}

// **[temp.param]/11: in a class template a pack is the last parameter.** A pack takes every
// argument that is left, so anything written after one could never be given a value.
void Parser::refuseAfterPack(const std::vector<TemplateParam> &params) {
    const std::size_t i = afterPack(params);
    if (i == static_cast<std::size_t>(-1)) return;
    src_.fail(params[i - 1].namePos, "'" + params[i - 1].name + "' is a parameter pack and takes every "
                                     "argument that is left, so '" + params[i].name + "' after it could "
                                     "never be given one");
}

// The name the template is being given, and nothing else about it. A class
// template's is read straight off the keyword; **a function template's sits behind
// a return type that mentions the parameters**, so `T` denotes a stand-in first.
std::string Parser::templatedName(const std::vector<TemplateParam> &params,
                                  bool *isClass, std::string *qualifier,
                                  bool *construction, const Type **pattern) {
    qualifier->clear();
    if (peek().is("struct") || peek().is("class") || peek().is("union")) {
        // [class]/1: the attributes, `alignas(T)` among them, stand between the class-key and the name (R26).
        std::size_t k = 1;
        for (;;) {
            if (peekAt(k).is("alignas") && peekAt(k + 1).is("(")) {
                int depth = 0;
                for (k++; peekAt(k).kind != TokenKind::End; k++) {
                    if (peekAt(k).is("(")) depth++;
                    else if (peekAt(k).is(")") && --depth == 0) { k++; break; }
                }
                continue;
            }
            if (peekAt(k).is("[") && peekAt(k + 1).is("[")) {
                for (k += 2; peekAt(k).kind != TokenKind::End && !(peekAt(k).is("]") && peekAt(k + 1).is("]")); k++) {}
                k += 2;
                continue;
            }
            break;
        }
        if (peekAt(k).kind != TokenKind::Ident)
            src_.fail(peek().pos, "this class template has no name");
        *isClass = true;
        return peekAt(k).text;
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
    // A non-type parameter stands for itself too, or `enable_if<N == 3, int>` folds N to 1 here.
    for (std::size_t i = 0; i < params.size(); i++)
        binding[i] = types_.templateParam(static_cast<int>(i));
    std::string name;
    const Type *fn = readTemplateDeclaration(scratch, binding, values, &name, qualifier, nullptr,
                                             construction);
    if (name.empty())
        src_.fail(peek().pos, "this function template has no name");
    if (pattern != nullptr) *pattern = fn;
    return name;
}

// **The parameters are bound to the argument list, and the tables are put back
// exactly as they were.** A type parameter becomes a type name and a non-type one
// an enumerator, which is what makes `T x` and `int a[N]` read with no new path.
void Parser::bindTemplateParameters(const std::vector<TemplateParam> &params,
                                    const std::vector<const Type *> &binding,
                                    const std::vector<long long> &values,
                                    const Packs &packs,
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
                s.hadPack = had->second;
            }
            PackBinding pb;
            pb.isTemplate = p.isTemplate;
            pb.inner = p.inner;
            // **A non-type pack is a list of values**, its type the written one or T's binding for `T... Vs`.
            pb.isValue = p.type != nullptr;
            if (pb.isValue) pb.valueType = paramTypeIn(p, binding);
            if (i < binding.size() && binding[i] != nullptr &&
                binding[i]->kind() == Kind::TemplateParam) {
                pb.types.push_back(binding[i]);       // reading a pattern
                if (pb.isValue) pb.values.push_back(0);
            } else if (i < packs.size()) {
                pb.types = packs[i];
                pb.values = packs[i].values;
            }
            packs_[p.name] = pb;
            undo->push_back(s);
            continue;
        }
        // **A template template parameter is an entry in `templates_` under its own name**: the
        // argument's declaration, so `C<int>` reads as `Box<int>` with nothing taught to any use; in
        // a pattern a stand-in `$TT<i>` with the parameter's own list, so `C<T>` reads as a pattern.
        if (p.isTemplate) {
            s.isTemplate = true;
            auto had = templates_.find(p.name);
            if (had != templates_.end()) { s.hadTemplate = true; s.hadDecl = had->second; }
            // It hides a type of the name around it - an enclosing template's parameter `A`, read in
            // `template <template <class> class A> ... A<T>`.
            auto hidden = typedefIndex_.find(p.name);
            if (hidden != typedefIndex_.end()) {
                s.had = true;
                s.was = hidden->second;
                typedefIndex_.erase(hidden);
            }
            auto bound = boundTypeParams_.find(p.name);
            if (bound != boundTypeParams_.end()) {
                s.hiddenBound = bound->second;
                boundTypeParams_.erase(bound);
            }
            const Type *b = i < binding.size() ? binding[i] : nullptr;
            if (b != nullptr && b->kind() == Kind::TemplateParam) {
                TemplateDecl stand;
                stand.name = "$TT" + std::to_string(b->length());
                stand.params = p.inner;
                stand.isClass = true;
                stand.ttParam = static_cast<int>(b->length());
                templates_[p.name] = stand;
            } else if (b != nullptr && b->isTemplateName() && templates_.count(templateKeyOf(b)) != 0) {
                const TemplateDecl named = templates_.find(templateKeyOf(b))->second;
                templates_[p.name] = named;
            } else {
                src_.fail(p.pos, "'" + p.name + "' is a template template parameter and this binds "
                                 "it to no template");
            }
            undo->push_back(s);
            continue;
        }
        if (s.isType) {
            auto it = typedefIndex_.find(p.name);
            if (it != typedefIndex_.end()) { s.had = true; s.was = it->second; }
            typedefIndex_[p.name] = typedefs_.size();
            typedefs_.push_back(TypedefName{ p.name, binding[i] });
            boundTypeParams_[p.name]++;
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
            boundValueParams_[p.name]++;
            undo->push_back(ev);
        } else {
            // **A value hides a pattern's parameter of the same name**: `Mx<3, 3>` built while a pattern
            // with an `N` of its own is read sees its own `N` as 3, not the pattern's.
            auto ref = nonTypePatternParams_.find(p.name);
            if (ref != nonTypePatternParams_.end()) {
                Shadow pr;
                pr.name = p.name;
                pr.isType = false;
                pr.isParamRef = true;
                pr.hadParamRef = true;
                pr.paramRefWas = ref->second;
                nonTypePatternParams_.erase(ref);
                undo->push_back(pr);
            }
            auto it = enumIndex_.find(p.name);
            if (it != enumIndex_.end()) { s.had = true; s.was = it->second; }
            enumIndex_[p.name] = enums_.size();
            EnumConst e{ p.name, values[i] };
            // [temp.param]/5: a non-type parameter is a prvalue of its declared type - an enumeration,
            // `unsigned`, `unsigned long long` - and not an int.
            const Type *pt = paramTypeIn(p, binding);
            if (pt != nullptr && p.typeAt != 0 && dependentType(pt)) {
                bool concrete = true;
                for (std::size_t k = 0; k < i && k < binding.size(); k++)
                    if (params[k].type == nullptr && !params[k].isPack && !params[k].isTemplate &&
                        (binding[k] == nullptr || dependentType(binding[k])))
                        concrete = false;
                if (concrete) pt = resolveParamType(params, i, binding, values);
            }
            if (pt != nullptr && pt->isInteger()) e.type = pt->unqualified();
            e.paramType = pt;
            enums_.push_back(e);
            boundValueParams_[p.name]++;
        }
        undo->push_back(s);
    }
}

// **[basic.scope.namespace]: one name, one template per namespace.** The first keeps the bare name, so a program
// with no two alike is what it was; the others are keyed by namespace, and found here.
std::string Parser::templateKeyFor(const TemplateDecl &decl) {
    const std::map<std::string, TemplateDecl>::const_iterator first = templates_.find(decl.name);
    if (first == templates_.end() || first->second.ns == decl.ns || first->second.ttParam >= 0) return decl.name;
    if (!decl.ownerTag.empty()) return decl.name;
    const std::string key = (decl.ns.empty() ? std::string("::") : decl.ns) + decl.name;
    std::vector<std::string> &keys = sameNamed_[decl.name];
    if (keys.empty()) keys.push_back(decl.name);
    if (std::find(keys.begin(), keys.end(), key) == keys.end()) keys.push_back(key);
    return key;
}

std::string Parser::templateKeyOf(const Type *t) const {
    const std::string &bare = t->templateName();
    const std::map<std::string, std::vector<std::string> >::const_iterator same = sameNamed_.find(bare);
    if (same != sameNamed_.end())
        for (const std::string &key : same->second) {
            const std::map<std::string, TemplateDecl>::const_iterator it = templates_.find(key);
            if (it != templates_.end() && it->second.ns == t->templateNamespace()) return key;
        }
    return bare;
}

std::map<std::string, Parser::TemplateDecl>::const_iterator
Parser::templateInScope(const std::string &written, const std::string &bare, const std::vector<std::string> &keys) const {
    // A template template parameter of the name, bound under it, hides them all.
    const std::map<std::string, TemplateDecl>::const_iterator own = templates_.find(bare);
    if (written == bare && own != templates_.end() && own->second.ttParam >= 0) return own;
    const auto declaredIn = [&](const std::string &ns) {
        const std::map<std::string, std::string>::const_iterator used = templateUsing_.find(ns + bare);
        if (used != templateUsing_.end()) return templates_.find(used->second);
        for (const std::string &key : keys) {
            const std::map<std::string, TemplateDecl>::const_iterator it = templates_.find(key);
            if (it != templates_.end() && it->second.ns == ns) return it;
        }
        for (const std::string &key : keys) {
            const std::map<std::string, TemplateDecl>::const_iterator it = templates_.find(key);
            if (it == templates_.end()) continue;
            for (const std::string &alias : inlineAliasPrefixes(it->second.ns))
                if (alias == ns) return it;
        }
        return templates_.end();
    };
    const std::string::size_type cut = written.rfind("::");
    if (cut == 0) return declaredIn(std::string());
    if (cut != std::string::npos) {
        std::string scope = written.substr(0, cut);
        const std::string::size_type first = scope.find("::");
        const std::string opened = namespaceNamed(scope.substr(0, first));
        if (!opened.empty()) scope = opened + (first == std::string::npos ? std::string() : scope.substr(first));
        const std::map<std::string, std::string>::const_iterator alias = namespaceAliases_.find(scope);
        if (alias != namespaceAliases_.end()) scope = alias->second;
        return declaredIn(scope + "::");
    }
    for (std::size_t depth = namespaceStack_.size(); depth > 0; depth--) {
        std::string prefix;
        for (std::size_t k = 0; k < depth; k++) prefix += namespaceStack_[k] + "::";
        const std::map<std::string, TemplateDecl>::const_iterator it = declaredIn(prefix);
        if (it != templates_.end()) return it;
    }
    // [namespace.udir]/2: what a directive names is found as though declared in the global namespace here.
    std::map<std::string, TemplateDecl>::const_iterator found = declaredIn(std::string());
    for (std::size_t i = usingNamespaces_.size(); i-- > 0; ) {
        const std::map<std::string, TemplateDecl>::const_iterator it = declaredIn(usingNamespaces_[i] + "::");
        if (it == templates_.end()) continue;
        if (found != templates_.end() && found != it)
            src_.fail(peek().pos, "'" + bare + "' is ambiguous: it names a template of '" + (found->second.ns.empty() ? std::string("::") : found->second.ns) +
                                  "' and one of '" + it->second.ns + "' - [namespace.udir]/6");
        found = it;
    }
    return found;
}

// [basic.lookup.unqual]: a namespace's template is found from inside it, through a using-directive or a
// using-declaration, or as a base's injected name; a name that is a binding of a template parameter is the parameter.
bool Parser::templateReachable(const TemplateDecl &decl, const std::string &written) const {
    if (decl.ns.empty() || decl.ttParam >= 0 || !decl.ownerTag.empty() || written != decl.name) return true;
    // A template's own text was looked up where it was written, and is read from wherever it was asked for.
    if (!boundTypeParams_.empty() || !boundValueParams_.empty() || !packs_.empty()) return true;
    if (namespacePrefix().compare(0, decl.ns.size(), decl.ns) == 0) return true;
    for (std::size_t depth = namespaceStack_.size() + 1; depth-- > 0; ) {
        std::string prefix;
        for (std::size_t k = 0; k < depth; k++) prefix += namespaceStack_[k] + "::";
        if (templateUsing_.count(prefix + written) != 0) return true;
    }
    // An inline namespace's members are its enclosing namespace's too.
    const auto opened = [&](const std::string &ns) {
        if (namespacePrefix().compare(0, ns.size(), ns) == 0) return true;
        for (const std::string &u : usingNamespaces_)
            if (ns == u + "::") return true;
        return false;
    };
    if (opened(decl.ns)) return true;
    for (std::string ns = decl.ns; ns.size() > 2; ) {
        const std::string path = ns.substr(0, ns.size() - 2);
        if (inlineNamespaces_.count(path) == 0) break;
        const std::string::size_type cut = path.rfind("::");
        ns = cut == std::string::npos ? std::string() : path.substr(0, cut + 2);
        if (opened(ns)) return true;
    }
    // A class in the way: `vector<int>` in a class derived from std::vector<int> is the injected name.
    const std::function<bool(const Type *)> hasBase = [&](const Type *c) {
        if (c == nullptr) return false;
        const Type *u = c->unqualified();
        if (u->isSpecialization() && u->templateName() == decl.name) return true;
        for (const Type::BaseSpec &b : u->bases())
            if (hasBase(b.type)) return true;
        return false;
    };
    for (const Type *c : classStack_)
        if (hasBase(c)) return true;
    return hasBase(currentClass_) || !inlineOwner_.empty();
}

// [namespace.qual]: `k::Box` is k's, or what k's using-declarations and inline namespaces bring in.
bool Parser::templateQualifiedBy(const std::string &written) const {
    const std::map<std::string, TemplateDecl>::const_iterator it = findTemplate(written);
    if (it == templates_.end()) return false;
    const std::string::size_type cut = written.rfind("::");
    if (cut == std::string::npos || cut == 0) return true;
    std::string scope = written.substr(0, cut);
    const std::string bare = written.substr(cut + 2);
    const std::string::size_type first = scope.find("::");
    const std::string opened = namespaceNamed(scope.substr(0, first));
    if (!opened.empty()) scope = opened + (first == std::string::npos ? std::string() : scope.substr(first));
    const std::map<std::string, std::string>::const_iterator alias = namespaceAliases_.find(scope);
    if (alias != namespaceAliases_.end()) scope = alias->second;
    if (namespaces_.count(scope) == 0) return true;   // a class's member template, asked elsewhere
    if (it->second.ns == scope + "::" || templateUsing_.count(scope + "::" + bare) != 0) return true;
    for (const std::string &ns : inlineAliasPrefixes(it->second.ns))
        if (ns == scope + "::") return true;
    // A directive in the namespace reaches on, and the directives are kept in one list.
    for (const std::string &u : usingNamespaces_)
        if (it->second.ns == u + "::") return true;
    return false;
}

void Parser::unbindTemplateParameters(const std::vector<Shadow> &undo) {
    for (std::size_t k = undo.size(); k-- > 0; ) {
        const Shadow &s = undo[k];
        if (s.isPack) {
            if (s.had) packs_[s.name] = s.hadPack;
            else       packs_.erase(s.name);
            continue;
        }
        if (s.isTemplate) {
            if (s.hadTemplate) templates_[s.name] = s.hadDecl;
            else               templates_.erase(s.name);
            if (s.had) typedefIndex_[s.name] = s.was;
            if (s.hiddenBound != 0) boundTypeParams_[s.name] = s.hiddenBound;
            continue;
        }
        if (s.isType) {
            if (s.had) typedefIndex_[s.name] = s.was;
            else       typedefIndex_.erase(s.name);
            if (--boundTypeParams_[s.name] <= 0) boundTypeParams_.erase(s.name);
        } else if (s.isParamRef) {
            if (s.hadParamRef) nonTypePatternParams_[s.name] = s.paramRefWas;
            else               nonTypePatternParams_.erase(s.name);
        } else {
            if (s.had) enumIndex_[s.name] = s.was;
            else       enumIndex_.erase(s.name);
            if (--boundValueParams_[s.name] <= 0) boundValueParams_.erase(s.name);
        }
    }
}

const Type *Parser::templateNameTypeOf(const TemplateDecl &decl) const {
    return types_.templateNameType(decl.name, decl.ns, decl.isAlias, decl.classKey);
}

// `$TT3` names template template parameter 3, and nothing a program writes begins with a `$`.
static bool ttRefName(const std::string &name, int *index) {
    if (name.compare(0, 3, "$TT") != 0) return false;
    *index = std::atoi(name.c_str() + 3);
    return true;
}

bool Parser::templateParamRef(const std::string &name, int *index) { return ttRefName(name, index); }

// What a template template parameter is bound to by a specialization: its template's stand-in, or
// - while two patterns are ordered against each other - the other pattern's own parameter.
const Type *Parser::templateOf(const Type *spec) const {
    int tt;
    if (ttRefName(spec->templateName(), &tt)) return types_.templateParam(tt);
    return templateNameTypeOf(templates_.find(templateKeyOf(spec))->second);
}

// **The rule is clang's, which takes P0522 as a defect report under -std=c++11** (C++11's letter is
// an exact match): a pack of types on either side stands for every type parameter the other has
// left, each other parameter wants one of its kind, and what the argument has over needs a default.
bool Parser::templateTemplateMatches(const std::vector<TemplateParam> &param,
                                     const std::vector<TemplateParam> &arg, std::string *why,
                                     std::size_t *at) {
    std::size_t a = 0;
    for (std::size_t p = 0; p < param.size(); p++) {
        const bool packHere = param[p].isPack, packThere = a < arg.size() && arg[a].isPack;
        if (packHere || packThere) {
            const std::vector<TemplateParam> &rest = packHere ? arg : param;
            for (std::size_t k = packHere ? a : p; k < rest.size(); k++)
                if (rest[k].type != nullptr || rest[k].isTemplate) {
                    *why = "a pack of types cannot stand for the " +
                           std::string(rest[k].isTemplate ? "template" : "non-type") + " parameter '" +
                           rest[k].name + "'";
                    *at = packHere ? param[p].namePos : rest[k].namePos;
                    return false;
                }
            return true;
        }
        if (a >= arg.size()) {
            *why = "it has " + std::to_string(arg.size()) + " template parameter(s) and the parameter's list has " +
                   std::to_string(param.size());
            return false;
        }
        const TemplateParam &P = param[p], &A = arg[a++];
        // A kind mismatch is reported where clang reports it: at the parameter of the list.
        if (P.isTemplate != A.isTemplate || (P.type == nullptr) != (A.type == nullptr)) {
            *why = "its parameter '" + A.name + "' is " +
                   (A.isTemplate ? "a template" : A.type != nullptr ? "a non-type" : "a type") +
                   " parameter where the parameter's list has " +
                   (P.isTemplate ? "a template" : P.type != nullptr ? "a non-type" : "a type") + " one";
            *at = P.namePos;
            return false;
        }
        if (P.isTemplate && !templateTemplateMatches(P.inner, A.inner, why, at)) return false;
    }
    for (; a < arg.size(); a++)
        if (!arg[a].isPack && arg[a].defBegin == 0) {
            *why = "its parameter '" + arg[a].name + "' has no default, and the parameter's list gives it nothing";
            return false;
        }
    return true;
}

// `Box`, `ns::NBox`, an alias template, or a template template parameter of the template around this one.
const Parser::TemplateDecl &Parser::templateTemplateArgument(const TemplateDecl &decl,
                                                              const TemplateParam &p) {
    const std::size_t pos = peek().pos;
    consume("::");
    if (peek().kind != TokenKind::Ident)
        src_.fail(pos, "'" + p.name + "' is a template template parameter of '" + decl.name +
                       "', so its argument is the name of a class template");
    std::string written = peek().text;
    at_++;
    while (peek().is("::") && peekAt(1).kind == TokenKind::Ident) {
        written += "::" + peekAt(1).text;
        at_ += 2;
    }
    if (peek().is("<"))
        src_.fail(pos, "'" + written + "' is given arguments here, and '" + p.name + "' is a template "
                       "template parameter, which takes a template's name alone");
    std::map<std::string, TemplateDecl>::const_iterator it = findTemplate(written);
    if (it == templates_.end() || !it->second.isClass)
        src_.fail(pos, "'" + written + "' does not name a class template or an alias template, "
                       "which is what '" + p.name + "' of '" + decl.name + "' takes");
    std::string why;
    std::size_t at = pos;
    if (!templateTemplateMatches(p.inner, it->second.params, &why, &at))
        src_.fail(at, "'" + written + "' cannot be the argument for the template template "
                      "parameter '" + p.name + "' of '" + decl.name + "': " + why);
    return it->second;
}

// `T *` and `const T &` with T's binding: the type of a non-type parameter written over an earlier one.
const Type *Parser::substituteParams(const Type *t, const std::vector<const Type *> &binding) const {
    if (t->unqualified() != t) return types_.withConst(substituteParams(t->unqualified(), binding));
    if (t->kind() == Kind::TemplateParam) {
        const std::size_t k = static_cast<std::size_t>(t->length());
        return k < binding.size() && binding[k] != nullptr ? binding[k] : t;
    }
    if (t->isPointer()) return types_.pointerTo(substituteParams(t->pointee(), binding));
    if (t->isReference()) {
        const Type *r = substituteParams(t->referent(), binding);
        if (r->isReference()) return t->isRValueReference() ? r : types_.referenceTo(r->referent());
        return t->isRValueReference() ? types_.rvalueReferenceTo(r) : types_.referenceTo(r);
    }
    return t;
}

std::vector<TemplateArg> Parser::argumentsFromBinding(
    const std::vector<TemplateParam> &params, const std::vector<const Type *> &binding,
    const std::vector<long long> &values, const Packs &packs) const {
    std::vector<TemplateArg> args;
    for (std::size_t i = 0; i < binding.size() && i < params.size(); i++) {
        TemplateArg a;
        if (params[i].type != nullptr && !params[i].isPack) {
            a.isType = false; a.type = paramTypeIn(params[i], binding); a.value = values[i];
            if (binding[i] != nullptr && binding[i]->kind() != Kind::TemplateParam) describeAddress(a);
        } else if (params[i].isPack) {
            a.isType = true; a.isPack = true; a.pack = packs[i];
            if (params[i].type != nullptr) {
                a.packIsValue = true; a.packValues = packs[i].values; a.type = paramTypeIn(params[i], binding);
            }
        } else {
            a.isType = true; a.type = binding[i]; a.isTemplate = params[i].isTemplate;
        }
        args.push_back(a);
    }
    return args;
}

// The declaration read again with the arguments in force. Nothing is
// registered: this answers what the signature *is*, and the caller decides
// what to do with it.
const Type *Parser::readTemplateDeclaration(const TemplateDecl &decl,
                                            const std::vector<const Type *> &binding,
                                            const std::vector<long long> &values,
                                            std::string *name,
                                            std::string *qualifier,
                                            const Packs *packs,
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
    // **The caller's locals are not in the template's scope**: `Mx<4,4> M; M * v` read the operator's
    // `CVector<M>` as that object, and the operator was never a candidate.
    const LocalsHidden hidden(this);
    // A friend template written in a class template: its class's parameters as that specialization bound them,
    // and the class around it for the class's own name (1.82).
    if (!decl.friendOf.empty() && !decl.classParams.empty())
        bindTemplateParameters(decl.classParams, decl.classBinding, decl.classValues, decl.classPacks, &guard.undo);
    struct FriendClass {
        Parser *p; bool pushed;
        ~FriendClass() { if (pushed) p->classStack_.pop_back(); }
    } friendClass{ this, false };
    if (!decl.friendOf.empty())
        if (const Type *scope = findTypedef(decl.friendOf)) {
            classStack_.push_back(scope);
            friendClass.pushed = true;
        }
    bindTemplateParameters(decl.params, binding, values,
                           packs != nullptr
                               ? *packs
                               : Packs(),
                           &guard.undo);
    const bool wasPattern = patternOnly_;
    for (std::size_t i = 0; i < binding.size(); i++)
        if (binding[i] != nullptr && binding[i]->kind() == Kind::TemplateParam)
            patternOnly_ = true;

    at_ = decl.afterParams;
    // `template <class Y> explicit P(Y *)`: the keyword is the class's to read, and says nothing of the type.
    if (decl.isConstructor && peek().is("explicit")) at_++;
    // **A constructor template has no type in front of its name**, so it is read the way a
    // held `X(` is: under its owner, which the declarator takes one-shot and clears.
    struct Owner {
        Parser *p; std::string tag, name, signature;
        ~Owner() { p->inlineOwner_ = tag; p->inlineOwnerName_ = name; p->signatureOwner_ = signature; }
    } owner{ this, inlineOwner_, inlineOwnerName_, signatureOwner_ };
    // And any member template's is read in its class, which `std::pair<iterator, bool> emplace(Args &&...)`
    // names a member type of.
    if (decl.isConstructor || (decl.isMember && !decl.ownerTag.empty())) {
        inlineOwner_ = decl.ownerTag;
        inlineOwnerName_ = decl.ownerLocal;
        signatureOwner_ = decl.ownerTag;
    }
    // **The signature names things in the namespace the template was declared in**, as its body
    // does when replayed: `template <class T> int adl(T, Tag)` inside NS reads NS's Tag from a call
    // outside NS (1.27, 2026-10-21, review of 1.25, item 3 - "expected a type" at the instantiation).
    struct Where {
        Parser *p; std::vector<std::string> was;
        ~Where() { p->namespaceStack_ = was; }
    } where{ this, namespaceStack_ };
    if (!decl.ns.empty()) namespaceStack_ = namespacePathStack(decl.ns);
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
        hasConstructors(d.type->tag())) {
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
        const std::size_t paramsOpen = at_;
        // **`constexpr` does not make the return type const** here either: the definition's replay
        // takes it off, and `constexpr T sq(T)` was refused for the two disagreeing (R16).
        if (quals.isConstexpr) d.type = types_.withoutConst(d.type);
        const Type *written = d.type;
        if (qualifier != nullptr && !d.qualifier.empty()) {
            // **A member of a class template defined outside it, read for its name**: its parameters may name
            // the class's nested types, `T A<T>::get(const S &)`, and a pattern is read shallow, with none to
            // find ([basic.scope.class]/1; 1.82: "this declaration has no type"). The replay reads them.
            bool read = false;
            std::size_t after = 0;
            try {
                Trial trial(this);
                parameterTypes(params, variadic);
                after = at_;
                read = true;
            } catch (const SubstitutionFailure &f) {
                if (f.unsupported) src_.fail(f.pos, f.why);
            }
            if (read) {
                at_ = after;
            } else {
                params.clear();
                variadic = false;
                at_ = paramsOpen;
                for (int depth = 0; peek().kind != TokenKind::End; at_++) {
                    if (peek().is("(")) depth++;
                    else if (peek().is(")") && --depth == 0) { at_++; break; }
                }
            }
        } else {
            parameterTypes(params, variadic);
        }
        d.type = types_.functionType(d.type, std::move(params), variadic);
        // **A bare `noexcept` or `throw()` on the signature is the
        // specialization's too**, read here so the definition's replay finds
        // the same promise; a dependent `noexcept(e)` waits for the body.
        {
            std::size_t q = at_;
            while (tokens_[q].is("const") || tokens_[q].is("volatile") || tokens_[q].is("&") ||
                   tokens_[q].is("&&"))
                q++;
            // **`noexcept(e)` read with the arguments bound** -
            // [except.spec]/1: the promise the definition's replay reads,
            // `noexcept(noexcept(X()))` at X = int; a dependent e stays.
            // A type parameter is bound to a type that depends on none; a pack to types that all are; a
            // non-type or template parameter has no type here to ask (1.79: any parameter without one made
            // the specification wait, so `noexcept(noexcept(x.swap(y)))` on a variadic template was never read).
            bool concrete = true;
            for (std::size_t k = 0; k < binding.size(); k++) {
                const bool typeParam = k >= decl.params.size() ||
                                       (decl.params[k].type == nullptr && !decl.params[k].isTemplate);
                if (!typeParam) continue;
                if (k < decl.params.size() && decl.params[k].isPack) {
                    if (packs == nullptr || k >= packs->size()) { concrete = false; continue; }
                    for (const Type *t : (*packs)[k])
                        if (t == nullptr || dependentType(t)) concrete = false;
                    continue;
                }
                if (binding[k] == nullptr || binding[k]->kind() == Kind::TemplateParam) concrete = false;
            }
            if (tokens_[q].is("noexcept") && tokens_[q + 1].is("(")) {
                if (concrete) {
                    const std::size_t back = at_;
                    // [basic.scope.block]/2: the parameters are in scope in the operand (1.77).
                    const bool named = noexceptOperandMayNameParameters(q + 2);
                    if (named) declareParametersAt(paramsOpen);
                    at_ = q + 2;
                    // A non-member template's operand names nothing of the class the call is written in:
                    // `swap(a, b)` there is not that class's member `swap`.
                    struct Outside {
                        Parser *p; bool on; std::vector<const Type *> classes; const Type *cls;
                        ~Outside() { if (on) { p->classStack_ = classes; p->currentClass_ = cls; } }
                    } outside{ this, !decl.isMember && !decl.isConstructor, classStack_, currentClass_ };
                    if (outside.on) { classStack_.clear(); currentClass_ = nullptr; }
                    patternNoexcept_ = constantExpression("a constant in 'noexcept('") != 0;
                    if (named) {
                        locals_.resize(scopeStarts_.back());
                        scopeStarts_.pop_back();
                    }
                    at_ = back;
                }
            } else {
                patternNoexcept_ = (tokens_[q].is("noexcept")) ||
                                   (tokens_[q].is("throw") && tokens_[q + 1].is("(") && tokens_[q + 2].is(")"));
            }
        }
        // `template <class T> auto f(T) -> T`: the pattern's return type is the trailing one.
        if (mentionsDeduced(written) && tokens_[skipFunctionQualifiers(at_)].is("->")) {
            at_ = skipFunctionQualifiers(at_);
            d.type = types_.functionType(readTrailingReturn(paramsOpen), d.type->params(), variadic);
        }
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
    bool body = false, initialiser = false;
    int depth = 0, parens = 0, brackets = 0;
    if (sawInit != nullptr) *sawInit = false;
    if (sawParen != nullptr) *sawParen = false;
    for (;;) {
        if (peek().kind == TokenKind::End)
            src_.fail(peek().pos, "this template's definition is never closed");
        // An array bound's parentheses are neither, `Tab<F, Seq<I...> >::v[sizeof...(I)];` (1.82: read as a
        // construction, and the static member was "declared here and not defined").
        if (depth == 0 && peek().is("[")) brackets++;
        if (depth == 0 && peek().is("]")) brackets--;
        if (brackets > 0) { at_++; continue; }
        // **An `=` at depth zero is an initialiser**, so what is being skipped is a definition even
        // though it has no braces - `template <class T> const R C<T>::k = R();` - and a `(` is a
        // parameter list or a construction; neither, and no braces, is `T Tm<T>::st;`, a definition.
        if (depth == 0 && peek().is("(")) parens++;
        if (depth == 0 && peek().is(")")) parens--;
        if (depth == 0 && parens == 0 && peek().is("=")) initialiser = true;
        if (sawInit != nullptr && depth == 0 && peek().is("=")) *sawInit = true;
        if (sawParen != nullptr && depth == 0 && peek().is("(")) *sawParen = true;
        // Braces after that `=` are a list, `C<T>::arr[2] = { 1, 2 };`, and no body.
        if (peek().is("{")) { depth++; body = body || !initialiser; at_++; continue; }
        if (peek().is("}")) {
            at_++;
            // A function try block's handlers follow its braces and are the body too.
            if (--depth == 0 && !(body && peek().is("catch"))) { consume(";"); return body; }
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
    // [temp]/4: a template, and a specialization of one, has C++ linkage.
    if (cLinkage_ > 0)
        src_.fail(peek().pos, "a template cannot have C linkage - [temp]/4; declare it outside "
                              "the 'extern \"C\"', or inside an 'extern \"C++\"'");
    at_++;
    if (!peek().is("<")) return explicitInstantiation(tokens_[at_ - 1].pos);
    if (peekAt(1).is(">")) return explicitSpecialization();
    templateParameters(decl.params);

    decl.afterParams = at_;
    decl.pos = peek().pos;

    // **An alias template is C++11 and begins with `using`** - [temp.alias].
    // It is asked before the scan below, which would see an '=' with no '('
    // in front of it and call a conforming C++11 program C++14.
    if (peek().is("using")) {
        refuseAfterPack(decl.params);
        at_++;
        decl.name = expectIdent("the alias template's name");
        expect("=");
        decl.aliasAt = at_;
        decl.isAlias = decl.isClass = decl.defined = true;
        decl.ns = namespacePrefix();
        decl.key = templateKeyFor(decl);
        if (templates_.count(decl.key) != 0)
            src_.fail(decl.pos, "'" + decl.name + "' is already a template");
        int depth = 0;
        while (peek().kind != TokenKind::End && !(depth == 0 && peek().is(";"))) {
            if (peek().is("(") || peek().is("{")) depth++;
            else if (peek().is(")") || peek().is("}")) depth--;
            at_++;
        }
        expect(";");
        templates_[decl.key] = decl;
        return true;
    }

    // **A variable template is C++14, and it is told from the two C++11
    // declarations by a token scan**: a class or function reaches '(' or a class
    // key before any '=', and an out-of-line member writes '::' before its own.
    if (!peek().is("struct") && !peek().is("class") && !peek().is("union")) {
        int depth = 0;
        for (std::size_t i = at_; i < tokens_.size(); i++) {
            const Token &t = tokens_[i];
            if (depth == 0) {
                // `operator<` names a function, and its `<` opens nothing.
                if (t.is("(") || t.is("::") || t.is("{") || t.is(";") || t.is("operator")) break;
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

    // A member template defined outside its class (`U S::conv(U)`, ...).
    if (outOfLineMemberTemplate(decl.params)) return true;

    // **`template <class T> Box<T>::Box(...)`**: from the `Box` after `::` it is shaped as a constructor
    // held inside the class, so it is kept on the class and replayed by that path.
    std::string special, nestedPath;
    if (atOutOfLineSpecial(&special, &nestedPath)) {
        TemplateDecl &owner = templateEntry(peek().text)->second;
        at_++;
        skipTemplateArguments();
        expect("::");
        std::string local = owner.name;
        while (peek().kind == TokenKind::Ident && peekAt(1).is("::")) {
            local = peek().text;
            at_ += 2;
        }
        TemplateDecl::OutOfLine ool;
        ool.start = at_;
        ool.constructor = special == "constructor";
        ool.destructor = !ool.constructor;
        ool.member = local;
        ool.nested = nestedPath;
        at_ = decl.afterParams;
        skipTemplatedDefinition();
        owner.outOfLine.push_back(ool);
        return true;
    }

    // **`template <class T> struct Box<T *>` - a partial specialization.** It is
    // told from the primary by the `<` after the name: a class template being
    // *declared* has nothing there, and one already declared is being specialized.
    const std::size_t q = peek().is("struct") || peek().is("class") || peek().is("union")
                        ? namespaceQualifierTokens(1) : 0;
    if ((peek().is("struct") || peek().is("class") || peek().is("union")) &&
        peekAt(1 + q).kind == TokenKind::Ident && peekAt(2 + q).is("<")) {
        std::string spelled;
        for (std::size_t k = 1; k < 1 + q; k++) spelled += peekAt(k).text;
        auto primary = templateEntry(spelled + peekAt(1 + q).text);
        if (primary == templates_.end() || !primary->second.isClass)
            src_.fail(peekAt(1 + q).pos, "'" + peekAt(1 + q).text + "' is not a class "
                                         "template, so there is nothing here to "
                                         "specialize");
        // [temp.param]/11 puts a pack last in a primary template; a partial's packs need only be deducible.
        // [temp.class.spec]/8: a partial specialization's parameters take no default arguments.
        for (const TemplateParam &p : decl.params)
            if (p.defBegin != 0)
                src_.fail(p.pos, "a partial specialization's template parameter cannot have a default "
                                 "argument - [temp.class.spec]/8");
        TemplateDecl::Partial ps;
        ps.params = decl.params;
        ps.pos = decl.pos;
        at_ += 2 + q;
        partialArguments(&ps, primary->second.params);
        // /8 too: the arguments are not the primary's own parameters in their own order.
        bool asPrimary = ps.args.size() == primary->second.params.size() && ps.args.size() == ps.params.size();
        for (std::size_t k = 0; k < ps.args.size() && asPrimary; k++) {
            const TemplateDecl::Partial::Arg &a = ps.args[k];
            if (a.isPackExpansion != primary->second.params[k].isPack) { asPrimary = false; break; }
            asPrimary = a.isPackExpansion ? a.param == k
                      : a.isType ? a.type != nullptr && a.type->kind() == Kind::TemplateParam &&
                                   a.type->unqualified() == a.type && static_cast<std::size_t>(a.type->length()) == k
                                 : a.isParam && a.param == k;
        }
        if (asPrimary)
            src_.fail(ps.pos, "this partial specialization's arguments are the primary template's own - it "
                              "specializes nothing, [temp.class.spec]/8");
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
                    // `R...` names its parameter: asked directly
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
        // [temp.class.spec]/8: a value given for `T v` may not specialize it while T is still a parameter.
        const std::vector<TemplateParam> &primaryParams = primary->second.params;
        for (std::size_t k = 0; k < ps.args.size() && k < primaryParams.size(); k++) {
            const int of = primaryParams[k].typeOf;
            if (ps.args[k].isType || ps.args[k].isPackExpansion || ps.args[k].isParam || of < 0 ||
                static_cast<std::size_t>(of) >= ps.args.size() || !ps.args[of].isType)
                continue;
            for (std::size_t i = 0; i < ps.params.size(); i++)
                if (mentionsParam(ps.args[of].type, i))
                    src_.fail(ps.pos, "this partial specialization gives '" + primaryParams[k].name +
                                      "' a value while its type '" + primaryParams[of].name + "' is still "
                                      "a parameter - a non-type argument may not specialize a parameter "
                                      "of dependent type");
        }
        at_ = decl.afterParams;
        skipTemplatedDefinition();
        primary->second.partials.push_back(ps);
        return true;
    }

    std::string qualifier;
    decl.classKey = peek().is("class");
    bool constructs = false;
    const Type *fnPattern = nullptr;
    decl.name = templatedName(decl.params, &decl.isClass, &qualifier, &constructs, &fnPattern);
    if (decl.isClass) refuseAfterPack(decl.params);
    else              requireDeducibleAfterPack(decl, fnPattern);
    // [temp.variadic]/5 where the template is written: `throw(Ts)` names a pack without its '...' - a
    // declaration nothing instantiates is ill-formed as much as one something does.
    if (!decl.isClass) {
        for (std::size_t k = decl.afterParams, depth = 0; k + 1 < tokens_.size() &&
                                                           tokens_[k].kind != TokenKind::End; k++) {
            const Token &t = tokens_[k];
            if (depth == 0 && (t.is(";") || t.is("{"))) break;
            if (t.is("(")) depth++;
            else if (t.is(")") && depth > 0) depth--;
            if (!t.is("throw") || !tokens_[k + 1].is("(")) continue;
            // Each entry of the list on its own: a pack named in it, and no '...' ending it.
            const Token *named = nullptr;
            for (std::size_t j = k + 2, inner = 0; j + 1 < tokens_.size() && tokens_[j].kind != TokenKind::End; j++) {
                const bool closes = tokens_[j].is(")") && inner == 0;
                if ((closes || (tokens_[j].is(",") && inner == 0)) && named != nullptr && !tokens_[j - 1].is("..."))
                    src_.fail(named->pos, "'" + named->text + "' is a parameter pack, and here it is written "
                                          "without the '...' that expands it");
                if (closes) break;
                if (tokens_[j].is(",") && inner == 0) { named = nullptr; continue; }
                if (tokens_[j].is("(") || tokens_[j].is("<")) inner++;
                else if ((tokens_[j].is(")") || tokens_[j].is(">")) && inner > 0) inner--;
                if (tokens_[j].kind == TokenKind::Ident && named == nullptr)
                    for (const TemplateParam &p : decl.params)
                        if (p.isPack && p.name == tokens_[j].text) named = &tokens_[j];
            }
        }
    }
    if (!decl.isClass && !literalSuffix(decl.name).empty())
        checkLiteralOperatorTemplate(decl, fnPattern);
    // Where it was written, for the manglers - the table's key stays bare.
    decl.ns = namespacePrefix();
    // [temp.local]/6: a template parameter is not redeclared in its scope - by the template's own name
    // either - and [temp.class.spec]/5's partial specialization is a class template's alone.
    for (const TemplateParam &p : decl.params)
        if (!p.name.empty() && p.name == decl.name && qualifier.empty())
            src_.fail(p.pos, "'" + p.name + "' is the name of a template parameter, and the template "
                             "cannot take it - [temp.local]/6");
    if (!decl.isClass && qualifier.empty() && decl.name.compare(0, 8, "operator") != 0)
        for (std::size_t i = decl.afterParams; i + 1 < tokens_.size() && !tokens_[i].is("(") &&
                                               !tokens_[i].is(";") && !tokens_[i].is("{"); i++)
            if (tokens_[i].kind == TokenKind::Ident && tokens_[i].text == decl.name && tokens_[i + 1].is("<"))
                src_.fail(tokens_[i].pos, "a function template cannot be partially specialized - "
                                          "[temp.class.spec] is for class templates; overload it instead");
    // [temp.param]/9: a default template argument is not written on a member defined outside its class.
    if (!qualifier.empty())
        for (const TemplateParam &p : decl.params)
            if (p.defBegin != 0)
                src_.fail(p.pos, "a default template argument cannot be written on a member defined outside "
                                 "its class template - [temp.param]/9");
    at_ = decl.afterParams;
    bool sawInit = false, sawParen = false;
    const bool defined = skipTemplatedDefinition(&sawInit, &sawParen);
    // [temp.local]/6 inside a class template's body: `int T;` where T is a parameter.
    if (decl.isClass && defined) {
        int depth = 0;
        for (std::size_t i = decl.afterParams; i + 1 < at_; i++) {
            if (tokens_[i].is("{")) { depth++; continue; }
            if (tokens_[i].is("}")) { depth--; continue; }
            if (depth == 0 || tokens_[i].kind != TokenKind::Ident) continue;
            const Token &prev = tokens_[i - 1], &next = tokens_[i + 1];
            const bool declares = (prev.kind == TokenKind::Ident || prev.is("int") || prev.is("char") ||
                                   prev.is("bool") || prev.is("long") || prev.is("short") || prev.is("double") ||
                                   prev.is("float") || prev.is("unsigned") || prev.is("signed")) &&
                                  (next.is(";") || next.is("=") || next.is("[") || next.is(",") || next.is(")"));
            for (const TemplateParam &p : decl.params)
                if (declares && !p.name.empty() && tokens_[i].text == p.name && !prev.is("::") &&
                    !(tokens_[i - 2].is("::")))
                    src_.fail(tokens_[i].pos, "'" + p.name + "' redeclares a template parameter inside the "
                                              "template - [temp.local]/6");
        }
    }

    // **A member of a class template defined outside it belongs to the class**, not
    // to a template of its own. The declarator already reads a qualified name; what
    // is new is that the qualifier is a template-id, naming the pattern.
    if (!qualifier.empty()) {
        const Type *of = findTypedef(qualifier);
        // A nested class of the pattern, `Tree<T>::It::Deep`: the member belongs to the template whose
        // specialization encloses it, and its path down is kept for the replay to find it by.
        std::string nestedIn;
        while (of != nullptr && !of->isSpecialization() && of->enclosing() != nullptr) {
            nestedIn = of->localName() + (nestedIn.empty() ? "" : "::" + nestedIn);
            of = of->enclosing();
        }
        // A pattern is read shallow, with no nested classes to find, so the qualifier's text is split:
        // the template-id up to its closing '>', then the path down.
        if (of == nullptr) {
            int depth = 0;
            for (std::size_t c = 0; c + 1 < qualifier.size(); c++) {
                if (qualifier[c] == '<') depth++;
                else if (qualifier[c] == '>') depth--;
                else if (depth == 0 && qualifier[c] == ':' && qualifier[c + 1] == ':') {
                    const Type *head = findTypedef(qualifier.substr(0, c));
                    if (head != nullptr && head->isSpecialization()) {
                        of = head;
                        nestedIn = qualifier.substr(c + 2);
                    }
                    break;
                }
            }
        }
        if (of == nullptr || !of->isSpecialization())
            src_.fail(decl.pos, "'" + qualifier + "' is not a class template, "
                                "so this defines a member of nothing");
        auto owner = templates_.find(templateKeyOf(of));
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
        ool.nested = nestedIn;
        owner->second.outOfLine.push_back(ool);
        return true;
    }

    // [dcl.fct.def.delete]/1: `= delete` is a definition - an explicit specialization may be written
    // for it - and every specialization the primary would make is deleted (R10).
    decl.isDeleted = !defined && !decl.isClass && at_ >= 3 && tokens_[at_ - 1].is(";") &&
                     tokens_[at_ - 2].is("delete") && tokens_[at_ - 3].is("=");
    decl.defined = defined || decl.isDeleted;
    // **Function templates overload; class templates do not.** A function
    // template of a name already seen is another overload, kept beside the
    // first; a call tries each by deduction.
    if (!decl.isClass) {
        // **[temp.decls]: a declaration is a candidate before any definition** - `declval`, the sizeof
        // idiom's `test`. A definition takes the place of the declaration it matches, and a specialization
        // already made from that declaration is read again, from the definition.
        std::vector<TemplateDecl> &set = fnTemplates_[decl.name];
        auto signatureOf = [&](const TemplateDecl &d) {
            std::vector<const Type *> self(d.params.size());
            for (std::size_t i = 0; i < self.size(); i++) self[i] = types_.templateParam(static_cast<int>(i));
            std::string ignored, key = "?" + std::to_string(d.afterParams);
            try {
                Trial trial(this);
                key = TypeTable::patternKey(readTemplateDeclaration(d, self, std::vector<long long>(self.size(), 0),
                                                                    &ignored));
            } catch (const SubstitutionFailure &) {
            }
            return key + "/" + std::to_string(d.params.size());
        };
        std::size_t same = set.size();
        if (!set.empty()) {
            const std::string mine = signatureOf(decl);
            for (std::size_t k = 0; k < set.size() && same == set.size(); k++)
                if (set[k].ns == decl.ns && signatureOf(set[k]) == mine) same = k;
        }
        if (same == set.size()) {
            set.push_back(decl);
        } else if (decl.defined && !set[same].defined) {
            for (Specialization &sp : specializations_)
                if (!sp.isClass && sp.start == set[same].afterParams) {
                    sp.start = decl.afterParams;
                    sp.emitted = false;
                }
            set[same] = decl;
        } else if (decl.defined) {
            set.push_back(decl);
        }
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
    decl.key = templateKeyFor(decl);
    auto it = templates_.find(decl.key);
    // [temp.param]/11-12 over the declarations together: the same kinds of parameter, each default given
    // once, and none followed by a parameter that has none.
    {
        const TemplateDecl *had = it != templates_.end() && it->second.isClass && !it->second.isAlias
                                ? &it->second : nullptr;
        if (had != nullptr) {
            bool same = had->params.size() == decl.params.size();
            for (std::size_t k = 0; k < decl.params.size() && same; k++) {
                const TemplateParam &a = had->params[k], &b = decl.params[k];
                same = (a.type == nullptr) == (b.type == nullptr) && a.isTemplate == b.isTemplate &&
                       a.isPack == b.isPack;
            }
            if (!same)
                src_.fail(decl.pos, "'" + decl.name + "' is redeclared with a different template parameter "
                                    "list - [temp.param]/1 with [temp.over.link]");
            for (std::size_t k = 0; k < decl.params.size(); k++)
                if (had->params[k].defBegin != 0 && decl.params[k].defBegin != 0)
                    src_.fail(decl.params[k].pos, "a template parameter's default argument is given twice - "
                                                  "[temp.param]/12");
        }
        bool seenDefault = false;
        for (std::size_t k = 0; k < decl.params.size(); k++) {
            const bool hasDefault = decl.params[k].defBegin != 0 || (had != nullptr && had->params[k].defBegin != 0);
            if (hasDefault) seenDefault = true;
            else if (seenDefault && !decl.params[k].isPack)
                src_.fail(decl.params[k].pos, "a template parameter without a default argument follows one "
                                              "with a default - [temp.param]/11");
        }
    }
    if (it == templates_.end()) {
        templates_[decl.key] = decl;
    } else if (decl.defined && !it->second.defined) {
        decl.outOfLine = it->second.outOfLine;
        // [temp.param]/10: the defaults of every declaration are the template's.
        for (std::size_t k = 0; k < decl.params.size() && k < it->second.params.size(); k++)
            if (decl.params[k].defBegin == 0) {
                decl.params[k].defBegin = it->second.params[k].defBegin;
                decl.params[k].defEnd = it->second.params[k].defEnd;
                decl.params[k].defNames = it->second.params[k].defNames;
                if (decl.params[k].defNames.empty() && decl.params[k].defBegin != 0)
                    for (std::size_t m = 0; m < k; m++) decl.params[k].defNames.push_back(it->second.params[m].name);
            }
        it->second = decl;
    } else if (decl.defined) {
        src_.fail(decl.pos, "'" + decl.name + "' is already a template, and "
                            "two class templates of one name are not supported "
                            "yet - a specialization is written 'template <> "
                            "struct " + decl.name + "<...>'");
    }
    return true;
}

// **[temp.mem]/1: a member template defined outside its class** (`S::conv`,
// `W<T>::both`): kept without its qualifier, as the class would hold it, and
// given to the class's member template, now or in a later specialization.
bool Parser::outOfLineMemberTemplate(const std::vector<TemplateParam> &outer) {
    const std::size_t save = at_;
    std::vector<TemplateParam> inner;
    const bool nested = peek().is("template") && peekAt(1).is("<");
    if (nested) {
        at_++;
        templateParameters(inner);
    }
    const std::size_t declStart = at_;
    // The declarator-id: `C::name (` or `C<...>::name (`, C a class, or a class template's own name - the name
    // an identifier or an operator-function-id, `operator()` and `operator<` as the class declared them.
    std::size_t qStart = 0, nameAt = 0;
    std::string owner, memberName;
    for (std::size_t i = declStart, depth = 0; tokens_[i].kind != TokenKind::End; i++) {
        const Token &t = tokens_[i];
        if (t.is("(") || t.is("{") || t.is(";") || t.is("=")) break;
        if (t.is("<")) { depth++; continue; }
        if (t.is(">")) { if (depth > 0) depth--; continue; }
        if (depth != 0 || t.kind != TokenKind::Ident) continue;
        std::size_t j = i + 1;
        if (tokens_[j].is("<")) {
            const std::size_t keep = at_;
            at_ = j;
            skipTemplateArguments();
            j = at_;
            at_ = keep;
        }
        if (!tokens_[j].is("::")) continue;
        std::string named;
        std::size_t paren = j + 2;
        if (tokens_[j + 1].kind == TokenKind::Ident) {
            named = tokens_[j + 1].text;
        } else if (tokens_[j + 1].is("operator")) {
            const Token &op = tokens_[j + 2];
            if ((op.is("(") && tokens_[j + 3].is(")")) || (op.is("[") && tokens_[j + 3].is("]"))) {
                named = "operator" + op.text + tokens_[j + 3].text;
                paren = j + 4;
            } else if ((op.is("new") || op.is("delete")) && tokens_[j + 3].is("[") && tokens_[j + 4].is("]")) {
                named = "operator " + op.text + "[]";
                paren = j + 5;
            } else if (op.kind != TokenKind::Ident && !op.is("(")) {
                named = "operator" + op.text;
                paren = j + 3;
            }
        }
        if (named.empty() || !tokens_[paren].is("(")) continue;
        if (nested) {
            const std::map<std::string, TemplateDecl>::const_iterator tm = findTemplate(t.text);
            if (tm == templates_.end() || !tm->second.isClass || tm->second.isAlias || j == i + 1) continue;
            owner = t.text;
        } else {
            const Type *cls = j == i + 1 ? findTypedef(t.text) : nullptr;
            if (cls == nullptr || !cls->isStructOrUnion() || memberTemplates_.count(cls->tag() + "::" + named) == 0)
                continue;
            owner = cls->tag();
        }
        qStart = i;
        nameAt = j + 1;
        memberName = named;
        break;
    }
    if (owner.empty()) {
        at_ = save;
        return false;
    }
    at_ = declStart;
    if (!skipTemplatedDefinition())
        src_.fail(tokens_[nameAt].pos, "'" + tokens_[nameAt].text + "' is declared again outside its class - a "
                                       "member template is defined there, [class.mfct]/1");
    const std::size_t defEnd = at_;
    OutOfLineMember m;
    m.copy = tokens_.size();
    for (std::size_t i = declStart; i < defEnd; i++)
        if (i < qStart || i >= nameAt) tokens_.push_back(tokens_[i]);
    m.copyEnd = tokens_.size();
    Token stop;
    stop.kind = TokenKind::End;
    tokens_.push_back(stop);
    for (const TemplateParam &p : nested ? inner : outer) m.innerNames.push_back(p.name);
    if (nested)
        for (const TemplateParam &p : outer) m.outerNames.push_back(p.name);
    const std::string key = owner + "::" + memberName;
    outOfLineMembers_[key].push_back(m);
    // Given to the member templates already declared: the class's, or every specialization's of the template.
    for (std::map<std::string, std::vector<TemplateDecl> >::iterator it = memberTemplates_.begin();
         it != memberTemplates_.end(); it++)
        for (TemplateDecl &mt : it->second) {
            if (mt.defined || mt.name != memberName) continue;
            const Type *cls = findTypedef(mt.ownerTag);
            const std::string of = nested ? (cls != nullptr && cls->isSpecialization() ? cls->templateName()
                                                                                       : std::string())
                                          : mt.ownerTag;
            if (of == owner) applyOutOfLineMemberTemplate(&mt);
        }
    at_ = defEnd;
    return true;
}

void Parser::applyOutOfLineMemberTemplate(TemplateDecl *mt) {
    const Type *cls = findTypedef(mt->ownerTag);
    const std::string owner = !mt->classParams.empty() && cls != nullptr && cls->isSpecialization()
                                  ? cls->templateName() : mt->ownerTag;
    std::map<std::string, std::vector<OutOfLineMember> >::const_iterator it =
        outOfLineMembers_.find(owner + "::" + mt->name);
    if (it == outOfLineMembers_.end()) return;
    for (const OutOfLineMember &m : it->second) {
        if (m.innerNames.size() != mt->params.size()) continue;
        // The definition's parameter names are the class's, as written there - `static` is said there alone.
        std::map<std::string, std::string> rename;
        for (std::size_t i = 0; i < m.innerNames.size(); i++)
            if (m.innerNames[i] != mt->params[i].name) rename[m.innerNames[i]] = mt->params[i].name;
        for (std::size_t i = 0; i < m.outerNames.size() && i < mt->classParams.size(); i++)
            if (m.outerNames[i] != mt->classParams[i].name) rename[m.outerNames[i]] = mt->classParams[i].name;
        std::size_t staticAt = 0;
        for (std::size_t i = mt->afterParams; tokens_[i].kind != TokenKind::End && !tokens_[i].is("(") &&
                                              !tokens_[i].is(";"); i++)
            if (tokens_[i].is("static")) staticAt = i;
        const std::size_t copy = tokens_.size();
        if (staticAt != 0) tokens_.push_back(tokens_[staticAt]);
        for (std::size_t i = m.copy; i < m.copyEnd; i++) {
            Token t = tokens_[i];
            if (t.kind == TokenKind::Ident) {
                std::map<std::string, std::string>::const_iterator r = rename.find(t.text);
                if (r != rename.end()) t.text = r->second;
            }
            tokens_.push_back(t);
        }
        Token stop;
        stop.kind = TokenKind::End;
        tokens_.push_back(stop);
        mt->afterParams = copy;
        mt->start = copy;
        mt->defined = true;
        return;
    }
}

// **[temp.explicit]: `template struct Box<int>;`, `template int twice<int>(int);`, `template double
// twice(double);`, `template char Box<char>::get() const;`** - an explicit instantiation definition. The
// specialization is made as a use would make it, and its definitions are emitted whether used or not.
bool Parser::explicitInstantiation(std::size_t pos) {
    const std::size_t declStart = at_;
    std::size_t end = at_;
    for (int depth = 0; tokens_[end].kind != TokenKind::End; end++) {
        if (tokens_[end].is("(") || tokens_[end].is("[") || tokens_[end].is("{")) depth++;
        else if (tokens_[end].is(")") || tokens_[end].is("]") || tokens_[end].is("}")) depth--;
        else if (depth == 0 && tokens_[end].is(";")) break;
    }
    if (tokens_[end].kind == TokenKind::End) src_.fail(pos, "this explicit instantiation never ends");
    // [temp.spec]/5: one explicit instantiation definition of a specialization in a program.
    auto once = [&](const std::string &what) {
        if (!explicitlyInstantiated_.insert(what).second)
            src_.fail(pos, "'" + what + "' is explicitly instantiated twice - [temp.spec]/5 allows one "
                           "explicit instantiation definition of it");
    };
    auto emitMembers = [&](const Type *cls, const std::string &only) {
        for (Signature &f : functions_) {
            const std::size_t cut = f.name.rfind("::");
            const std::string leaf = cut == std::string::npos ? f.name : f.name.substr(cut + 2);
            if (f.owner == cls->tag() && !f.implicit && !f.deleted && (only.empty() || leaf == only)) {
                f.used = true;
                // Emitted whether this unit calls it or not - another unit's `extern template` relies on it (1.81:
                // a member of a class template's specialization is inline, and the unused ones were pruned).
                explicitlyInstantiatedSymbols_.insert(f.symbol);
            }
        }
    };
    // A class: every member it defines is instantiated, [temp.explicit]/8.
    if (peek().is("struct") || peek().is("class") || peek().is("union")) {
        at_++;
        const std::map<std::string, TemplateDecl>::const_iterator t =
            peek().kind == TokenKind::Ident && peekAt(1).is("<") ? findTemplate(peek().text) : templates_.end();
        if (t == templates_.end() || !t->second.isClass)
            src_.fail(peek().pos, "an explicit instantiation of a class names a class template's specialization");
        const std::size_t tpos = peek().pos;
        // [temp.explicit]/4: the template's definition must come first.
        if (!t->second.defined)
            src_.fail(tpos, "'" + t->second.name + "' is declared and not defined here, and an explicit "
                            "instantiation definition needs the definition - [temp.explicit]/4");
        at_++;
        const Type *cls = instantiateClass(t->second, tpos);
        if (cls == nullptr || !cls->isStructOrUnion() || !peek().is(";"))
            src_.fail(tpos, "this explicit instantiation of a class is not '" + t->second.name + "<...>;'");
        once(cls->tag());
        emitMembers(cls, std::string());
        at_ = end + 1;
        return true;
    }
    // A member of a class template's specialization: `Box<char>::get`.
    for (std::size_t i = declStart; i < end; i++) {
        if (tokens_[i].kind != TokenKind::Ident || !tokens_[i + 1].is("<")) continue;
        const std::map<std::string, TemplateDecl>::const_iterator t = findTemplate(tokens_[i].text);
        if (t == templates_.end() || !t->second.isClass || t->second.isAlias) continue;
        at_ = i + 1;
        skipTemplateArguments();
        if (!peek().is("::") || peekAt(1).kind != TokenKind::Ident) break;
        const std::string member = peekAt(1).text;
        at_ = i + 1;
        const Type *cls = instantiateClass(t->second, tokens_[i].pos);
        if (cls == nullptr || !cls->isStructOrUnion())
            src_.fail(tokens_[i].pos, "'" + t->second.name + "<...>' names no class here");
        once(cls->tag() + "::" + member);
        emitMembers(cls, member);
        at_ = end + 1;
        return true;
    }
    // A function template's specialization, its arguments written or deduced from the declared type.
    std::size_t nameAt = 0;
    for (std::size_t i = declStart, depth = 0; i < end; i++) {
        const Token &t = tokens_[i];
        if (t.is("(") && depth == 0 && nameAt != 0) break;
        if (t.is("(")) depth++;
        else if (t.is(")") && depth > 0) depth--;
        else if (t.kind == TokenKind::Ident && depth == 0 && fnTemplates_.count(t.text) != 0 &&
                 (tokens_[i + 1].is("<") || tokens_[i + 1].is("("))) nameAt = i;
    }
    if (nameAt == 0)
        src_.fail(pos, "this explicit instantiation names no class template's specialization, member of "
                       "one, or function template");
    const std::string name = tokens_[nameAt].text;
    std::size_t argsEnd = nameAt + 1;
    if (tokens_[nameAt + 1].is("<")) {
        at_ = nameAt + 1;
        skipTemplateArguments();
        argsEnd = at_;
    }
    const std::size_t copy = tokens_.size();
    for (std::size_t i = declStart; i < end; i++)
        if (i <= nameAt || i >= argsEnd) tokens_.push_back(tokens_[i]);
    Token stop;
    stop.kind = TokenKind::End;
    tokens_.push_back(stop);
    TemplateDecl plain;
    plain.afterParams = copy;
    std::string ignored;
    const Type *wanted = readTemplateDeclaration(plain, std::vector<const Type *>(), std::vector<long long>(),
                                                 &ignored);
    const std::vector<TemplateDecl> overloads = fnTemplates_[name];
    for (std::size_t k = 0; k < overloads.size(); k++) {
        const TemplateDecl &decl = overloads[k];
        std::vector<const Type *> binding;
        std::vector<long long> values;
        Packs packs;
        std::vector<TemplateArg> args;
        std::string why;
        if (argsEnd != nameAt + 1) {
            at_ = nameAt + 1;
            try {
                Trial trial(this);
                templateArguments(decl, &binding, &values, &args, &packs);
            } catch (const SubstitutionFailure &f) {
                if (f.unsupported) src_.fail(f.pos, f.why);
                continue;
            }
        } else {
            if (!deduceTemplateArguments(decl, wanted->params(), &binding, &values, &packs, &why)) continue;
            args = argumentsFromBinding(decl.params, binding, values, packs);
        }
        if (readTemplateDeclaration(decl, binding, values, &ignored, nullptr, &packs)->describe() !=
            wanted->describe())
            continue;
        if (!decl.defined)
            src_.fail(pos, "'" + name + "' is declared and not defined here, and an explicit instantiation "
                           "definition needs the definition - [temp.explicit]/4");
        const std::string symbol = instantiate(decl, binding, values, args, pos, packs).symbol;
        once(symbol);
        for (Signature &f : functions_)
            if (f.symbol == symbol) f.used = true;
        at_ = end + 1;
        return true;
    }
    src_.fail(pos, "this explicit instantiation matches no declaration of the function template '" + name + "'");
    return false;
}

bool Parser::functionExplicitSpecialization(std::size_t pos) {
    if (peek().is("template")) return memberTemplateExplicitSpecialization(pos);
    const std::size_t declStart = at_;
    std::size_t nameAt = 0, depth = 0;
    for (std::size_t i = declStart; i < tokens_.size() && tokens_[i].kind != TokenKind::End; i++) {
        const Token &t = tokens_[i];
        if (t.is("(") && depth == 0 && nameAt != 0) break;
        if (t.is("(")) depth++;
        else if (t.is(")") && depth > 0) depth--;
        else if (t.kind == TokenKind::Ident && depth == 0 && fnTemplates_.count(t.text) != 0 &&
                 (tokens_[i + 1].is("<") || tokens_[i + 1].is("("))) nameAt = i;
        if (t.is(";") || t.is("{")) break;
    }
    if (nameAt == 0 && memberExplicitSpecialization(pos)) return true;
    if (nameAt == 0)
        src_.fail(peek().pos, "an explicit specialization of anything but a class template, a "
                              "namespace-scope function template or a member function of a class "
                              "template's specialization is not supported yet");
    const std::string name = tokens_[nameAt].text;
    std::size_t argsEnd = nameAt + 1;
    if (tokens_[nameAt + 1].is("<")) {
        at_ = nameAt + 1;
        skipTemplateArguments();
        argsEnd = at_;
    }
    at_ = declStart;
    if (!skipTemplatedDefinition())
        src_.fail(pos, "an explicit specialization of '" + name + "' declared without its "
                       "definition is not supported yet - give it its body here");
    const std::size_t defEnd = at_;

    // The declaration again without its `<...>`, appended to the stream, as a lambda's body is.
    const std::size_t copy = tokens_.size();
    for (std::size_t i = declStart; i < defEnd; i++)
        if (i <= nameAt || i >= argsEnd) tokens_.push_back(tokens_[i]);
    Token stop;
    stop.kind = TokenKind::End;
    tokens_.push_back(stop);
    TemplateDecl plain;
    plain.afterParams = copy;
    std::string ignored;
    const Type *wanted = readTemplateDeclaration(plain, std::vector<const Type *>(),
                                                 std::vector<long long>(), &ignored);

    struct Match {
        std::size_t which;
        std::vector<const Type *> binding;
        std::vector<long long> values;
        Packs packs;
        std::vector<TemplateArg> args;
        const Type *pattern;
    };
    std::vector<Match> matches;
    // [temp.expl.spec]/2 with [namespace.def]/8: the templates the name reaches where it is written - the
    // namespace it names or is in, and the inline namespaces in that one; never a sibling namespace's.
    std::string scope;
    {
        std::vector<std::string> parts;
        std::size_t k = nameAt;
        while (k >= 2 && tokens_[k - 1].is("::") && tokens_[k - 2].kind == TokenKind::Ident) {
            parts.insert(parts.begin(), tokens_[k - 2].text);
            k -= 2;
        }
        if (parts.empty()) {
            const std::string here = namespacePrefix();
            scope = here.empty() ? std::string() : here.substr(0, here.size() - 2);
        } else {
            scope = k >= 1 && tokens_[k - 1].is("::") ? parts[0] : namespaceNamed(parts[0]);
            for (std::size_t i = 1; i < parts.size() && !scope.empty(); i++) scope += "::" + parts[i];
        }
    }
    std::string reachedIn;
    std::vector<TemplateDecl> overloads = scope.empty() ? std::vector<TemplateDecl>()
                                                        : templatesNamed(name, scope, &reachedIn);
    if (overloads.empty() && scope.empty()) overloads = fnTemplates_[name];
    for (std::size_t k = 0; k < overloads.size(); k++) {
        const TemplateDecl &decl = overloads[k];
        Match m;
        m.which = k;
        std::string why;
        if (argsEnd != nameAt + 1) {
            at_ = nameAt + 1;
            try {
                Trial trial(this);
                templateArguments(decl, &m.binding, &m.values, &m.args, &m.packs);
            } catch (const SubstitutionFailure &f) {
                if (f.unsupported) src_.fail(f.pos, f.why);
                continue;
            }
        } else {
            if (!deduceTemplateArguments(decl, wanted->params(), &m.binding, &m.values, &m.packs, &why))
                continue;
            m.args = argumentsFromBinding(decl.params, m.binding, m.values, m.packs);
        }
        const Type *made = readTemplateDeclaration(decl, m.binding, m.values, &ignored, nullptr, &m.packs);
        if (made->describe() != wanted->describe()) continue;
        std::vector<const Type *> self(decl.params.size());
        for (std::size_t i = 0; i < self.size(); i++) self[i] = types_.templateParam(static_cast<int>(i));
        m.pattern = readTemplateDeclaration(decl, self, m.values, &ignored);
        matches.push_back(m);
    }
    // [temp.expl.spec]/12 with [temp.func.order]: of the primaries it fits, the most specialized.
    struct Order {
        static bool atLeast(const Parser &p, const Type *a, const Type *b) {
            const std::vector<const Type *> &pa = a->params(), &pb = b->params();
            if (pa.size() != pb.size()) return false;
            std::vector<const Type *> binding(pb.size() + pa.size());
            std::string why;
            for (std::size_t i = 0; i < pa.size(); i++)
                if (!p.matchPattern(pb[i], pa[i], &binding, &why)) return false;
            return true;
        }
    };
    for (std::size_t c = 0; c < matches.size(); c++) {
        bool best = true;
        for (std::size_t o = 0; o < matches.size() && best; o++)
            if (o != c && !(Order::atLeast(*this, matches[c].pattern, matches[o].pattern) &&
                            !Order::atLeast(*this, matches[o].pattern, matches[c].pattern)))
                best = false;
        if (!best) continue;
        const Match &m = matches[c];
        const std::size_t before = functions_.size();
        instantiate(overloads[m.which], m.binding, m.values, m.args, pos, m.packs);
        if (functions_.size() == before)
            src_.fail(pos, "'" + specializationKey(name, m.args) + "' has already been used further up, "
                           "so specializing it here is too late - the specialization goes before the first use");
        specializations_.back().start = copy;
        functions_.back().used = true;          // a definition the program wrote, emitted as one
        functions_.back().deleted = false;      // its own, not the deleted primary's (R10)
        at_ = defEnd;
        return true;
    }
    src_.fail(pos, matches.empty() ? "this explicit specialization matches no declaration of the function "
                                     "template '" + name + "'"
                                   : "this explicit specialization fits more than one declaration of '" +
                                     name + "' and none is more specialized than the rest");
    return false;
}

// **`template <> int S<int>::f() { ... }` - [temp.expl.spec]/1, a member function of one specialization
// written out (R7).** The class is made as a use would make it, the template's body for that member is
// dropped, and these tokens are read as the member's definition outside the class.
bool Parser::memberExplicitSpecialization(std::size_t pos) {
    const std::size_t declStart = at_;
    std::size_t idAt = 0;
    for (std::size_t i = declStart; i + 1 < tokens_.size() && tokens_[i].kind != TokenKind::End; i++) {
        if (tokens_[i].is("(") || tokens_[i].is(";") || tokens_[i].is("{") || tokens_[i].is("=")) break;
        if (tokens_[i].kind == TokenKind::Ident && tokens_[i + 1].is("<")) {
            std::map<std::string, TemplateDecl>::const_iterator t = findTemplate(tokens_[i].text);
            if (t != templates_.end() && t->second.isClass && !t->second.isAlias) { idAt = i; break; }
        }
    }
    if (idAt == 0) return false;
    at_ = idAt + 1;
    skipTemplateArguments();
    if (!peek().is("::")) { at_ = declStart; return false; }
    const bool isData = peekAt(1).kind == TokenKind::Ident && (peekAt(2).is("=") || peekAt(2).is("{"));
    if (!(peekAt(1).kind == TokenKind::Ident && peekAt(2).is("(")) && !isData)
        src_.fail(pos, "an explicit specialization of a member class or a member template of a class "
                       "template is not supported yet - only of a member function or a static data member");
    const std::string member = peekAt(1).text;
    const std::string templateName = tokens_[idAt].text;
    at_ = idAt + 1;
    const Type *cls = instantiateClass(templateEntry(templateName)->second, tokens_[idAt].pos);
    if (cls == nullptr || !cls->isStructOrUnion())
        src_.fail(pos, "'" + templateName + "<...>' names no class here");
    // `template <> int S<int>::s = 2;`: the template's own definition of s is not made for S<int>.
    if (isData) {
        const std::vector<TemplateDecl::OutOfLine> &ool = templateEntry(templateName)->second.outOfLine;
        for (std::size_t i = 0; i < specializations_.size(); i++) {
            if (!specializations_[i].isClass || specializations_[i].key != cls->tag()) continue;
            std::vector<bool> &done = specializations_[i].outsideDone;
            done.resize(ool.size(), false);
            for (std::size_t k = 0; k < ool.size(); k++)
                if (ool[k].isData && ool[k].member == member && ool[k].nested.empty()) done[k] = true;
        }
        at_ = declStart;
        topLevel(*current_);
        return true;
    }
    // [temp.expl.spec]/6: before the member's first use, which is where its body would be made.
    const std::vector<std::size_t> *set = overloadsOf(cls->tag() + "::" + member);
    if (set == nullptr)
        src_.fail(pos, "'" + cls->describe() + "' has no member function '" + member + "' to specialize");
    std::vector<std::size_t> overloads = *set;
    std::vector<PendingBody> held;
    for (std::size_t i = 0; i < specializations_.size(); i++) {
        if (!specializations_[i].isClass || specializations_[i].key != cls->tag()) continue;
        std::vector<PendingBody> &bodies = specializations_[i].bodies;
        for (std::size_t b = 0; b < bodies.size(); ) {
            if (std::find(overloads.begin(), overloads.end(), bodies[b].which) != overloads.end()) {
                held.push_back(bodies[b]);
                bodies.erase(bodies.begin() + static_cast<std::ptrdiff_t>(b));
            } else {
                b++;
            }
        }
        std::vector<bool> wasDefined(overloads.size());
        for (std::size_t k = 0; k < overloads.size(); k++) {
            if (functions_[overloads[k]].used)
                src_.fail(pos, "'" + cls->describe() + "::" + member + "' has already been used further "
                               "up, so specializing it here is too late - the specialization goes before "
                               "the first use");
            wasDefined[k] = functions_[overloads[k]].defined;
            functions_[overloads[k]].defined = false;
        }
        at_ = declStart;
        topLevel(*current_);
        // The overloads this definition does not define keep the template's bodies, as they were.
        for (std::size_t k = 0; k < overloads.size(); k++) {
            if (functions_[overloads[k]].defined) continue;
            functions_[overloads[k]].defined = wasDefined[k];
            for (std::size_t h = 0; h < held.size(); h++)
                if (held[h].which == overloads[k])
                    specializations_[i].bodies.push_back(held[h]);   // the table may have moved
        }
        return true;
    }
    src_.fail(pos, "'" + cls->describe() + "' was not made from its template here, so it has no member to specialize");
    return false;
}

// **A member template of one class specialization, written out** -
// [temp.expl.spec]/16: `S<int>::g(char)` specializes g<char>, `S<char>::g(U)`
// defines g anew; the tokens are copied without their `S<int>::`.
bool Parser::memberTemplateExplicitSpecialization(std::size_t pos) {
    at_++;
    std::vector<TemplateParam> ownParams;
    const bool full = peek().is("<") && peekAt(1).is(">");
    if (full) at_ += 2;
    else templateParameters(ownParams);
    const std::size_t declStart = at_;
    std::size_t idAt = 0;
    for (std::size_t i = declStart; i + 1 < tokens_.size() && tokens_[i].kind != TokenKind::End; i++) {
        if (tokens_[i].is("(") || tokens_[i].is(";") || tokens_[i].is("{")) break;
        if (tokens_[i].kind == TokenKind::Ident && tokens_[i + 1].is("<")) {
            std::map<std::string, TemplateDecl>::const_iterator t = findTemplate(tokens_[i].text);
            if (t != templates_.end() && t->second.isClass && !t->second.isAlias) { idAt = i; break; }
        }
    }
    if (idAt == 0)
        src_.fail(pos, "'template <> template' names no member of a class template's specialization");
    at_ = idAt + 1;
    skipTemplateArguments();
    const std::size_t colons = at_;
    if (!peek().is("::") || peekAt(1).kind != TokenKind::Ident)
        src_.fail(pos, "'template <> template' names no member of a class template's specialization");
    const std::string member = peekAt(1).text;
    at_ = idAt + 1;
    const Type *cls = instantiateClass(templateEntry(tokens_[idAt].text)->second, tokens_[idAt].pos);
    if (cls == nullptr || !cls->isStructOrUnion())
        src_.fail(pos, "'" + tokens_[idAt].text + "<...>' names no class here");
    std::map<std::string, std::vector<TemplateDecl> >::iterator set =
        memberTemplates_.find(cls->tag() + "::" + member);
    if (set == memberTemplates_.end() || set->second.empty())
        src_.fail(pos, "'" + cls->describe() + "' has no member template '" + member + "' to specialize");
    at_ = declStart;
    if (!skipTemplatedDefinition())
        src_.fail(pos, "an explicit specialization of '" + cls->describe() + "::" + member + "' declared "
                       "without its definition is not supported yet - give it its body here");
    const std::size_t defEnd = at_;
    // `g<long>(long)`: the arguments written after the member's name are left out too - the parameter types say them.
    std::size_t argsFrom = colons + 2, argsEnd = colons + 2;
    if (tokens_[argsFrom].is("<")) {
        at_ = argsFrom;
        skipTemplateArguments();
        argsEnd = at_;
    }
    const std::size_t copy = tokens_.size();
    for (std::size_t i = declStart; i < defEnd; i++)
        if ((i < idAt || i > colons) && (i < argsFrom || i >= argsEnd)) tokens_.push_back(tokens_[i]);
    Token stop;
    stop.kind = TokenKind::End;
    tokens_.push_back(stop);

    if (!full) {
        // S<char>'s member template, defined anew: every use of it after this reads these tokens.
        for (TemplateDecl &mt : set->second) {
            mt.params = ownParams;
            mt.afterParams = copy;
            mt.start = copy;
        }
        at_ = defEnd;
        return true;
    }
    // The specialization these parameters ask for, made from these tokens - the arguments deduced from
    // the parameter types written, as a full specialization of a free function template has them.
    TemplateDecl plain;
    plain.afterParams = copy;
    std::string ignored;
    const Type *wanted = readTemplateDeclaration(plain, std::vector<const Type *>(),
                                                 std::vector<long long>(), &ignored);
    const std::vector<TemplateDecl> candidates = set->second;
    for (std::size_t k = 0; k < candidates.size(); k++) {
        const TemplateDecl &mt = candidates[k];
        std::vector<const Type *> binding;
        std::vector<long long> values;
        Packs packs;
        std::string why;
        Bound bound(this);
        std::vector<Shadow> &undo = bound.undo;
        if (!mt.classParams.empty())
            bindTemplateParameters(mt.classParams, mt.classBinding, mt.classValues, mt.classPacks, &undo);
        const bool ok = deduceTemplateArguments(mt, wanted->params(), &binding, &values, &packs, &why);
        // The template's own declaration at these arguments must be the one written, its return type included.
        const Type *declared = ok ? readTemplateDeclaration(mt, binding, values, &ignored, nullptr, &packs) : nullptr;
        bound.release();
        if (!ok || declared == nullptr || declared->describe() != wanted->describe()) continue;
        const std::vector<TemplateArg> args = argumentsFromBinding(mt.params, binding, values, packs);
        TemplateDecl written = mt;
        written.afterParams = copy;
        written.start = copy;
        const Signature *made = instantiateMemberTemplate(written, binding, values, args, pos);
        if (made == nullptr) break;
        const std::size_t index = static_cast<std::size_t>(made - &functions_[0]);
        functions_[index].used = true;
        // Found where the template's own specialization would be: a later `s.g('x')` takes this one.
        const std::string display = specializationKey(mt.isConstructor ? mt.ownerLocal : mt.name, args);
        memberTemplateMade_[mt.ownerTag + "::" + display + "@" + std::to_string(mt.afterParams)] = index;
        at_ = defEnd;
        return true;
    }
    src_.fail(pos, "this explicit specialization matches no member template '" + member + "' of '" +
                   cls->describe() + "'");
    return false;
}

// `template <> struct Box<int> { ... };` - rung 5.6. A class written out for one
// argument list: the tag is `Box<int>` as the template would have made it, so every
// lookup and mangling is the same. **The list is read against the primary's.**
bool Parser::explicitSpecialization() {
    const std::size_t pos = peek().pos;
    expect("<");
    takeClosingAngle();

    if (!peek().is("struct") && !peek().is("class") && !peek().is("union"))
        return functionExplicitSpecialization(pos);
    const Kind kind = peek().is("union") ? Kind::Union : Kind::Struct;
    const bool isClass = peek().is("class");
    at_++;
    // `template <> struct S<int>::In { ... };` - a member class of S<int>, not S<int> itself.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("<")) {
        const auto t = findTemplate(peek().text);
        if (t != templates_.end() && t->second.isClass && !t->second.isAlias) {
            const std::size_t back = at_;
            const std::size_t wasSplit = angleSplit_;
            at_++;
            skipTemplateArguments();
            const bool member = peek().is("::") && peekAt(1).kind == TokenKind::Ident &&
                                (peekAt(2).is("{") || peekAt(2).is(":"));
            at_ = back;
            angleSplit_ = wasSplit;
            if (member) {
                const TemplateDecl primary = t->second;
                at_++;
                memberClassSpecialization(primary, kind, isClass, pos);
                return true;
            }
        }
    }

    // `template <> struct lib::Box<int>`: a namespace qualifier, an inline one's left out or not.
    at_ += namespaceQualifierTokens(0);
    if (peek().kind != TokenKind::Ident)
        src_.fail(peek().pos, "this specialization names no class");
    const std::string name = peek().text;
    auto primary = templateEntry(name);
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

    const std::string tag = specializationKey(tagName(primary->second), args);
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

void Parser::memberClassSpecialization(const TemplateDecl &primary, Kind kind, bool isClass, std::size_t pos) {
    const Type *owner = instantiateClass(primary, pos);
    expect("::");
    const std::string member = peek().text;
    const std::string tag = owner->tag() + "::" + member;
    const auto lazy = lazyMemberClasses_.find(tag);
    if (lazy == lazyMemberClasses_.end()) {
        const Type *had = lookupInClass(owner, member);
        if (had != nullptr && had->isComplete())
            src_.fail(peek().pos, "'" + tag + "' has already been used further up, so specializing it here is "
                                  "too late - the specialization goes before the first use");
        src_.fail(peek().pos, "'" + member + "' is not a member class of '" + owner->tag() + "'");
    }
    if (lazy->second.done)
        src_.fail(peek().pos, "'" + tag + "' has already been used further up, so specializing it here is too "
                              "late - the specialization goes before the first use");
    lazy->second.done = true;
    lazy->second.replaced = true;
    // An ordinary class nested in the specialization: no parameter is bound, and its bodies are read now.
    std::vector<const Type *> outerClasses;
    outerClasses.swap(classStack_);
    std::vector<const Type *> chain;
    for (const Type *e = owner; e != nullptr; e = e->enclosing()) chain.insert(chain.begin(), e);
    classStack_ = chain;
    const auto outerParams = instantiatingParams_;
    const auto outerBinding = instantiatingBinding_;
    const auto outerValues = instantiatingValues_;
    instantiatingParams_.clear();
    instantiatingBinding_.clear();
    instantiatingValues_.clear();
    const Type *made = structOrUnionSpecifier(kind, isClass);
    instantiatingParams_ = outerParams;
    instantiatingBinding_ = outerBinding;
    instantiatingValues_ = outerValues;
    classStack_.swap(outerClasses);
    if (!made->isComplete())
        src_.fail(pos, "an explicit specialization is a definition, and this one has no body");
    expect(";");
}

const Type *Parser::realizeMemberClass(const Type *t, bool required) {
    if (t == nullptr) return t;
    const Type *e = t;
    while (e->isArray()) e = e->pointee();
    e = e->unqualified();
    if (!e->isStructOrUnion() || e->isComplete()) return t;
    const auto pending = pendingSpecializations_.find(e->tag());
    if (pending != pendingSpecializations_.end()) {
        if ((classesInstantiating_ > 0 || typedefSpecifiers_) && !required) return t;
        const auto d = templates_.find(pending->second.templateName);
        if (d == templates_.end() || !d->second.defined) return t;
        const PendingSpecialization held = pending->second;
        pendingSpecializations_.erase(pending);
        const TemplateDecl decl = d->second;
        instantiateClassWith(decl, held.binding, held.values, held.args, held.packs, peek().pos);
        return t;
    }
    const auto it = lazyMemberClasses_.find(e->tag());
    if (it == lazyMemberClasses_.end() || it->second.done) return t;
    const LazyMemberClass lazy = it->second;
    if (!required && !classStack_.empty() && classStack_.back() == lazy.owner && !lazy.owner->isComplete())
        return t;
    const std::string tag = it->first;
    // Done once it is defined; a substitution failure inside leaves it to be asked for again.
    struct Once {
        LazyMemberClass *l;
        bool ok;
        ~Once() { if (!ok) l->done = false; }
    } once{ &it->second, false };
    it->second.done = true;
    const Nesting deep(this, tokens_[lazy.start].pos, "template instantiation");

    // Read as the class template's body read it: its classes around it, its arguments bound, its namespace.
    struct Restore {
        Parser *p;
        std::size_t at, angle;
        std::vector<const Type *> classes;
        const Type *current;
        std::string inlineOwner, realizing;
        std::vector<TemplateParam> params;
        std::vector<const Type *> binding;
        std::vector<long long> values;
        std::vector<std::string> namespaces;
        bool deferring, pattern;
        std::vector<Shadow> undo;
        ~Restore() {
            p->unbindTemplateParameters(undo);
            p->at_ = at;
            p->angleSplit_ = angle;
            p->classStack_.swap(classes);
            p->currentClass_ = current;
            p->inlineOwner_ = inlineOwner;
            p->realizingMember_ = realizing;
            p->instantiatingParams_ = params;
            p->instantiatingBinding_ = binding;
            p->instantiatingValues_ = values;
            p->namespaceStack_ = namespaces;
            p->deferSpecializationBodies_ = deferring;
            p->patternOnly_ = pattern;
        }
    } restore{ this, at_, angleSplit_, std::vector<const Type *>(), currentClass_, inlineOwner_, realizingMember_,
               instantiatingParams_, instantiatingBinding_, instantiatingValues_, namespaceStack_,
               deferSpecializationBodies_, patternOnly_, std::vector<Shadow>() };
    restore.classes.swap(classStack_);
    for (const Type *e = lazy.owner; e != nullptr; e = e->enclosing()) classStack_.insert(classStack_.begin(), e);
    currentClass_ = nullptr;
    inlineOwner_.clear();
    realizingMember_ = tag;
    instantiatingParams_ = lazy.params;
    instantiatingBinding_ = lazy.binding;
    instantiatingValues_ = lazy.values;
    namespaceStack_ = lazy.namespaces;
    deferSpecializationBodies_ = true;
    patternOnly_ = false;
    bindTemplateParameters(lazy.params, lazy.binding, lazy.values, lazy.packs, &restore.undo);
    const LocalsHidden hidden(this);
    at_ = lazy.start;
    structOrUnionSpecifier(lazy.kind, lazy.isClass);

    // **Its bodies are the specialization's**, replayed with it when used. One still being read takes them
    // itself at its end; one already made has its record, found by the outermost specialization's tag.
    const Type *spec = nullptr;
    for (const Type *e = lazy.owner; e != nullptr; e = e->enclosing())
        if (e->isSpecialization()) spec = e;
    if (spec != nullptr && spec->isComplete())
        for (std::size_t i = 0; i < specializations_.size(); i++) {
            if (!specializations_[i].isClass || specializations_[i].key != spec->tag()) continue;
            std::vector<PendingBody> rest;
            for (const PendingBody &b : pendingBodies_)
                if (b.tag == tag || b.tag.compare(0, tag.size() + 2, tag + "::") == 0)
                    specializations_[i].bodies.push_back(b);
                else
                    rest.push_back(b);
            pendingBodies_.swap(rest);
            break;
        }
    once.ok = true;
    return t;
}

// The tokens read without being consumed. skipTemplateArguments is what walks
// the argument list, so the `>>` split has to be put back too.
bool Parser::atOutOfLineSpecial(std::string *what, std::string *nested) {
    if (peek().kind != TokenKind::Ident) return false;
    auto t = findTemplate(peek().text);
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
        // `Tree<T>::It::It(`: the class whose constructor this is may be nested, a name per `::`.
        std::string path, cls = name;
        while (peekAt(n).kind == TokenKind::Ident && peekAt(n + 1).is("::") &&
               (peekAt(n + 2).kind == TokenKind::Ident || peekAt(n + 2).is("~"))) {
            cls = peekAt(n).text;
            path += (path.empty() ? "" : "::") + cls;
            n += 2;
        }
        if (peekAt(n).is("~")) { n++; *what = "destructor"; }
        else                   { *what = "constructor"; }
        if (peekAt(n).kind == TokenKind::Ident && peekAt(n).text == cls &&
            peekAt(n + 1).is("(")) yes = true;
        if (nested != nullptr) *nested = path;
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

bool Parser::spliceArgumentPacks(const TemplateDecl &decl, std::vector<Shadow> *undo, std::size_t *resumeAt,
                                 std::size_t *resumeSplit) {
    if (!peek().is("<")) return false;
    const std::size_t open = at_, wasSplit = angleSplit_;
    skipTemplateArguments();
    const std::size_t endAt = at_, endSplit = angleSplit_;
    at_ = open;
    angleSplit_ = wasSplit;
    // Closed by a `>>`'s first half the list ends at that token; by its second half, the first closed a list inside.
    const bool firstHalf = endSplit == endAt && tokens_[endAt].is(">>");
    const std::size_t close = firstHalf ? endAt : endAt - 1;
    const bool innerHalf = !firstHalf && tokens_[close].is(">>");

    std::vector<std::pair<std::size_t, std::size_t> > written;
    int depth = 0;
    std::size_t from = open + 1;
    for (std::size_t k = open + 1; k < close; k++) {
        const Token &t = tokens_[k];
        // a group is stepped over whole: the `>` of `decltype(a > b)` closes nothing
        if (t.is("(") || t.is("[") || t.is("{")) { const std::size_t g = groupCloseAt(k); if (g < close) { k = g; continue; } depth++; }
        else if (t.is(")") || t.is("]") || t.is("}")) depth--;
        else if (t.is("<") && tokens_[k - 1].kind == TokenKind::Ident && isTemplateName(tokens_[k - 1].text)) depth++;
        else if (t.is(">")) depth--;
        else if (t.is(">>")) depth -= 2;
        else if (t.is(",") && depth == 0) { written.push_back(std::make_pair(from, k)); from = k + 1; }
    }
    if (close > open + 1) written.push_back(std::make_pair(from, close));

    // Needed where an expansion lands on a parameter that is not a pack.
    std::size_t packAt = decl.params.size();
    for (std::size_t i = 0; i < decl.params.size(); i++)
        if (decl.params[i].isPack) { packAt = i; break; }
    auto expansion = [&](std::size_t j) {
        const std::size_t b = written[j].first;
        if (written[j].second != b + 2 || tokens_[b].kind != TokenKind::Ident || !tokens_[b + 1].is("..."))
            return packs_.end();
        const auto pk = packs_.find(tokens_[b].text);
        return pk != packs_.end() && !pk->second.standsForItself() ? pk : packs_.end();
    };
    bool needed = false;
    for (std::size_t j = 0, index = 0; j < written.size(); j++) {
        const auto pk = expansion(j);
        if (pk == packs_.end()) { index++; continue; }
        if (index < packAt && !pk->second.types.empty()) needed = true;
        // An empty expansion where the template has no pack is no argument at all: `A<T, Args...>` for One<T>.
        if (packAt == decl.params.size() && pk->second.types.empty()) needed = true;
        index += pk->second.types.size();
    }
    if (!needed) return false;

    const std::size_t copy = tokens_.size();
    Token mark = tokens_[open];
    tokens_.push_back(mark);
    bool first = true;
    for (std::size_t j = 0; j < written.size(); j++) {
        Token comma = tokens_[open];
        comma.kind = TokenKind::Punct;
        comma.text = ",";
        const auto pk = expansion(j);
        if (pk == packs_.end()) {
            if (!first) tokens_.push_back(comma);
            first = false;
            for (std::size_t k = written[j].first; k < written[j].second; k++) {
                const Token t = tokens_[k];
                tokens_.push_back(t);
            }
            continue;
        }
        const PackBinding pb = pk->second;
        for (std::size_t m = 0; m < pb.types.size(); m++) {
            if (!first) tokens_.push_back(comma);
            first = false;
            Token name = tokens_[written[j].first];
            // A function parameter pack's members are parameters already; any other's are bound under a name.
            if (m < pb.names.size() && !pb.names[m].empty()) {
                name.text = pb.names[m];
            } else {
                name.text = "$pk" + std::to_string(splicedNames_++);
                TemplateParam member;
                member.name = name.text;
                if (pb.isValue) member.type = pb.valueType;
                bindTemplateParameters(std::vector<TemplateParam>(1, member),
                                       std::vector<const Type *>(1, pb.isValue ? pb.valueType : pb.types[m]),
                                       std::vector<long long>(1, pb.isValue ? pb.values[m] : 0), Packs(1), undo);
            }
            tokens_.push_back(name);
        }
    }
    Token closer = tokens_[close];
    closer.text = ">";
    if (innerHalf) tokens_.push_back(closer);
    tokens_.push_back(closer);
    Token stop;
    stop.kind = TokenKind::End;
    tokens_.push_back(stop);
    at_ = copy;
    *resumeAt = endAt;
    *resumeSplit = endSplit;
    return true;
}

// `<int, 3>` at a use, read against the parameter list it is for. A type parameter
// takes a type-id and a non-type one a constant expression, so which is which is
// decided by the template and never by the shape of what is written.
void Parser::templateArguments(const TemplateDecl &decl,
                               std::vector<const Type *> *binding,
                               std::vector<long long> *values,
                               std::vector<TemplateArg> *args,
                               Packs *packs,
                               std::size_t *given) {
    std::vector<Shadow> spliceUndo;
    struct Splice {
        Parser *p;
        std::vector<Shadow> *u;
        bool on;
        std::size_t at, split;
        ~Splice() {
            p->unbindTemplateParameters(*u);
            if (on) { p->at_ = at; p->angleSplit_ = split; }
        }
    } splice{ this, &spliceUndo, false, 0, 0 };
    splice.on = spliceArgumentPacks(decl, &spliceUndo, &splice.at, &splice.split);
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
        // [temp.arg.explicit]/3: a function template's trailing arguments not written are deduced from the call.
        if (given != nullptr && !decl.isClass && atClosingAngle() && (p.isPack || p.defBegin == 0)) {
            *given = i;
            break;
        }
        if (!decl.isClass && atClosingAngle() && (p.isPack ? i > 0 : p.defBegin == 0 && p.type == nullptr))
            src_.fail(peek().pos, "'" + decl.name + "' is given " + std::to_string(i) + " of its template "
                                  "arguments, and deducing the rest from the call is not supported yet - write them all");
        if (!defaulted && i > 0 && !(p.isPack && atClosingAngle()) && !consume(","))
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
            if (undo.empty() || !p.defNames.empty())
                for (std::size_t k = 0; k < i; k++) {
                    std::vector<TemplateParam> one(1, decl.params[k]);
                    if (k < p.defNames.size()) one[0].name = p.defNames[k];
                    std::vector<const Type *> ob(1, (*binding)[k]);
                    std::vector<long long> ov(1, (*values)[k]);
                    Packs op(1, std::vector<const Type *>());
                    bindTemplateParameters(one, ob, ov, op, &undo);
                }
            const std::size_t saved = at_;
            // The default is read where it was written: in the template's namespace, as the deduced path does.
            const std::vector<std::string> outerStack = namespaceStack_;
            struct BackTo {
                std::vector<std::string> &stack; const std::vector<std::string> &was;
                ~BackTo() { stack = was; }
            } backTo{ namespaceStack_, outerStack };
            if (!decl.ns.empty()) namespaceStack_ = namespacePathStack(decl.ns);
            at_ = p.defBegin;
            if (p.isTemplate) {
                const TemplateDecl &named = templateTemplateArgument(decl, p);
                a.isType = a.isTemplate = true;
                a.type = named.ttParam >= 0 ? types_.templateParam(named.ttParam) : templateNameTypeOf(named);
                binding->push_back(a.type); values->push_back(0);
            } else if (p.type == nullptr) {
                StorageClass sc; Qualifiers quals;
                const Type *base = specifiers(&sc, &quals);
                Declared d = declarator(base, true);
                binding->push_back(d.type); values->push_back(0);
                a.isType = true; a.type = d.type;
            } else if (patternOnly_ && dependentAhead(true)) {
                // `bool = fp_of<A>::ok && fp_of<B>::ok` while a signature is read: the earlier arguments are
                // parameters, so the default is a value of them, kept as the tree as a written one is.
                const Type *pt = paramTypeIn(p, *binding);
                const std::size_t from = at_;
                DepExpr tree = *dependentExpression(true, false);
                tree.text = depText(from, at_);
                binding->push_back(pt); values->push_back(0);
                a.isType = false; a.type = pt; a.dependentValue = true; a.expr = types_.intern(tree);
            } else {
                const Type *pt = paramTypeIn(p, *binding);
                const long long v = takesAddress(pt) ? addressArgument(pt, peek().pos)
                                                     : constantExpression("a default template argument");
                binding->push_back(pt); values->push_back(v);
                a.isType = false; a.type = pt; a.value = v;
                describeAddress(a);
            }
            at_ = saved;
            args->push_back(a);
            std::vector<TemplateParam> one(1, p);
            std::vector<const Type *> ob(1, binding->back());
            std::vector<long long> ov(1, values->back());
            Packs op(1, std::vector<const Type *>());
            bindTemplateParameters(one, ob, ov, op, &undo);
            continue;
        }
        // **A pack takes everything that is left**, including nothing. It is
        // the last parameter by construction, so there is no ambiguity about
        // where it stops: the closing angle stops it.
        if (p.isPack) {
            a.isPack = true;
            a.isType = true;
            // **A non-type pack takes values of its type** - the written one, or T's binding for `T... Vs`.
            a.packIsValue = p.type != nullptr;
            std::size_t expandedAt = 0;      // where a pattern's `Ns...` was written
            std::size_t expansionEnd = 0;    // the members up to and including it
            if (a.packIsValue) {
                a.type = paramTypeIn(p, *binding);
                if (!a.type->isInteger() && a.type->kind() != Kind::TemplateParam)
                    src_.fail(p.pos, "a non-type template parameter pack of type '" + a.type->describe() +
                                     "' is not supported yet - it must be an integral or enumeration type");
            }
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
                        const PackBinding &pb = pk->second;
                        if (pb.isValue != a.packIsValue)
                            src_.fail(peek().pos, "'" + peek().text + "' is a pack of " +
                                                  (pb.isValue ? "values" : "types") + " and '" + p.name +
                                                  "' of '" + decl.name + "' takes " +
                                                  (a.packIsValue ? "values" : "types"));
                        at_ += 2;
                        // **In a pattern the pack stands for itself**, and the argument is its expansion -
                        // `JXspT_EE`, `JDpT_E` - which deduction binds from the members it is given.
                        if (pb.standsForItself()) {
                            if (a.expandsParam >= 0)
                                src_.fail(tokens_[at_ - 2].pos, "two parameter packs expanded in one pattern's "
                                                                "argument list are not supported yet");
                            expandedAt = tokens_[at_ - 2].pos;
                            a.expandsParam = static_cast<int>(pb.types[0]->length());
                            expansionEnd = a.pack.size() + 1;
                            a.pack.push_back(pb.types[0]);
                            if (a.packIsValue) a.packValues.push_back(0);
                            continue;
                        }
                        for (std::size_t k = 0; k < pb.types.size(); k++) {
                            a.pack.push_back(pb.types[k]);
                            if (a.packIsValue) a.packValues.push_back(pb.values[k]);
                        }
                        continue;
                    }
                }
                if (p.isTemplate) {
                    const TemplateDecl &named = templateTemplateArgument(decl, p);
                    a.pack.push_back(named.ttParam >= 0 ? types_.templateParam(named.ttParam) : templateNameTypeOf(named));
                    continue;
                }
                if (a.packIsValue) {
                    // `List<M>` in a signature, M a non-type parameter: the member is a reference to it, `XT0_E`.
                    if (peek().kind == TokenKind::Ident && nonTypePatternParams_.count(peek().text) != 0 &&
                        (peekAt(1).is(",") || peekAt(1).is(">") || peekAt(1).is(">>"))) {
                        a.pack.push_back(types_.templateParam(nonTypePatternParams_[peek().text]));
                        a.packValues.push_back(0);
                        at_++;
                        continue;
                    }
                    if (expandPackValues(a)) continue;
                    const std::size_t argPos = peek().pos;
                    const long long v = constantExpression("a template argument");
                    checkNonTypeArgument(a.type, v, argPos);
                    a.pack.push_back(a.type);
                    a.packValues.push_back(v);
                    continue;
                }
                // **`Tup<typename Un<T>::type...>`, `Tup<T &...>`** - [temp.variadic]/4: a pattern over packs. In a
                // signature, where they stand for themselves, it is read once with each bound to its stand-in and
                // is the one expansion `Dp <pattern>`; in an instantiation, once per member.
                {
                    std::size_t members = 0;
                    const std::size_t end = packPatternEnd(true, &members);
                    if (end != 0) {
                        std::vector<TemplateParam> stand;
                        std::vector<const Type *> standing;
                        for (std::size_t k = at_; k + 1 < end; k++) {
                            const auto pk = tokens_[k].kind == TokenKind::Ident ? packs_.find(tokens_[k].text)
                                                                                : packs_.end();
                            if (pk == packs_.end() || !pk->second.standsForItself() || pk->second.isValue) continue;
                            bool had = false;
                            for (const TemplateParam &q : stand) had = had || q.name == tokens_[k].text;
                            if (had) continue;
                            TemplateParam q;
                            q.name = tokens_[k].text;
                            q.isTemplate = pk->second.isTemplate;
                            q.inner = pk->second.inner;
                            stand.push_back(q);
                            standing.push_back(pk->second.types[0]);
                        }
                        if (!stand.empty()) {
                            std::vector<Shadow> undo;
                            struct Unbind {
                                Parser *p; std::vector<Shadow> *u;
                                ~Unbind() { p->unbindTemplateParameters(*u); }
                            } unbind{ this, &undo };
                            bindTemplateParameters(stand, standing, std::vector<long long>(stand.size(), 0),
                                                   Packs(stand.size()), &undo);
                            StorageClass psc;
                            const Type *pattern = declarator(specifiers(&psc), true).type;
                            expect("...");
                            a.pack.push_back(types_.packExpansion(pattern));
                            continue;
                        }
                        const std::size_t from = at_;
                        for (std::size_t m = 0; m < members; m++) {
                            std::vector<Shadow> undo;
                            struct Unbind {
                                Parser *p; std::vector<Shadow> *u;
                                ~Unbind() { p->unbindTemplateParameters(*u); }
                            } unbind{ this, &undo };
                            at_ = from;
                            bindPatternMember(from, end, m, &undo);
                            StorageClass msc;
                            a.pack.push_back(declarator(specifiers(&msc), true).type);
                        }
                        at_ = end;
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
            // `List<Ns..., 4>` is `JXspT_ELi4EE`: an expansion followed by other arguments, not matched here.
            // `Tup<H, T...>` - fixed members, then the expansion taking the rest - is.
            if (a.expandsParam >= 0 && a.pack.size() != expansionEnd)
                src_.fail(expandedAt, "a parameter pack expanded beside other arguments in a template's "
                                      "signature, '" + decl.name + "<" + p.name + "..., ...>', is not "
                                      "supported yet - the expansion alone is");
            binding->push_back(nullptr);
            values->push_back(0);
            if (packs != nullptr) { (*packs)[i] = a.pack; (*packs)[i].values = a.packValues; }
            args->push_back(a);
            break;
        }
        // **A template's name, for a template template parameter** - a pattern's `Holder<C, T>`
        // records the parameter itself, as a type argument naming T does.
        if (p.isTemplate) {
            const TemplateDecl &named = templateTemplateArgument(decl, p);
            a.isType = a.isTemplate = true;
            a.type = named.ttParam >= 0 ? types_.templateParam(named.ttParam) : templateNameTypeOf(named);
            binding->push_back(a.type);
            values->push_back(0);
            args->push_back(a);
            continue;
        }
        if (p.type == nullptr) {
            StorageClass sc;
            Qualifiers quals;
            const Type *base = specifiers(&sc, &quals);
            Declared d;
            if (peek().is("(") && !atParenDeclarator()) d.type = functionTypeArgument(base);
            else d = declarator(base, true);
            if (!d.name.empty())
                src_.fail(d.pos, "a template argument is a type here, and this "
                                 "names something");
            binding->push_back(d.type);
            values->push_back(0);
            a.isType = true;
            a.type = d.type;
        } else {
            const Type *pt = paramTypeIn(p, *binding);
            // `small<char, 0>` for `typename EI<sizeof(T) == 1, int>::type N`: its type once T is bound.
            if (p.typeAt != 0 && dependentType(pt) && !patternOnly_) {
                bool concrete = true;
                for (std::size_t k = 0; k < i && k < binding->size(); k++)
                    if (decl.params[k].type == nullptr && !decl.params[k].isPack && !decl.params[k].isTemplate &&
                        ((*binding)[k] == nullptr || dependentType((*binding)[k])))
                        concrete = false;
                if (concrete) pt = resolveParamType(decl.params, i, *binding, *values);
            }
            if (!pt->isInteger() && pt->kind() != Kind::TemplateParam && pt->kind() != Kind::DependentMember &&
                !takesAddress(pt))
                src_.fail(p.pos, "a non-type template parameter cannot be of type '" +
                                 pt->describe() + "' - [temp.param]/4 allows an integral, enumeration, "
                                 "pointer, reference, pointer to member or std::nullptr_t type");
            // **`V<N>` while a signature is read for deduction**: a bare name
            // that is a pattern non-type parameter is a reference to it, not a
            // value to fold. Anything else is an ordinary constant expression.
            if (peek().kind == TokenKind::Ident &&
                nonTypePatternParams_.count(peek().text) != 0 &&
                (peekAt(1).is(",") || peekAt(1).is(">") || peekAt(1).is(">>"))) {
                a.isType = false;
                a.isParam = true;
                a.paramIndex = nonTypePatternParams_[peek().text];
                a.type = pt;
                binding->push_back(pt);
                values->push_back(0);
                at_++;
            } else if (patternOnly_ && dependentAhead(true)) {
                // **An argument that is an expression of the parameters** - `sizeof(T) == 4`,
                // `N + M` - is kept as the tree the Itanium name spells, `X...E`, and folds nothing.
                const std::size_t from = at_;
                DepExpr tree = *dependentExpression(true, false);
                tree.text = depText(from, at_);
                a.isType = false;
                a.type = pt;
                a.dependentValue = true;
                a.expr = types_.intern(tree);
                binding->push_back(pt);
                values->push_back(0);
            } else if (takesAddress(pt)) {
                const long long v = addressArgument(pt, peek().pos);
                binding->push_back(pt);
                values->push_back(v);
                a.isType = false;
                a.type = pt;
                a.value = v;
                describeAddress(a);
            } else {
                const std::size_t argPos = peek().pos;
                bool namesParam = false;
                if (patternOnly_)
                    for (std::size_t k = 0, depth = 0; peekAt(k).kind != TokenKind::End; k++) {
                        const Token &t = peekAt(k);
                        if (t.is("(") || t.is("[")) depth++;
                        else if ((t.is(")") || t.is("]")) && depth > 0) depth--;
                        else if (depth == 0 && (t.is(",") || t.is(">") || t.is(">>"))) break;
                        else if (t.kind == TokenKind::Ident && nonTypePatternParams_.count(t.text) != 0)
                            namesParam = true;
                        // A type parameter bound to a type the call wrote is that type: `sizeof(To) != 0`
                        // with To explicit is the value 1, and enable_if<1, D> the real class.
                        else if (t.kind == TokenKind::Ident && boundTypeParams_.count(t.text) != 0) {
                            const Type *bt = findTypedef(t.text);
                            if (bt == nullptr || dependentType(bt)) namesParam = true;
                        }
                    }
                const long long v = constantExpression("a template argument");
                checkNonTypeArgument(pt, v, argPos);
                binding->push_back(pt);
                values->push_back(v);
                a.isType = false;
                a.type = pt;
                a.value = v;
                a.dependentValue = namesParam;
            }
        }
        args->push_back(a);
    }
    if (!atClosingAngle())
        src_.fail(peek().pos, "'" + decl.name + "' takes " +
                              std::to_string(decl.params.size()) +
                              " template arguments and this gives more");
    if (given != nullptr && *given != static_cast<std::size_t>(-1)) {
        takeClosingAngle();
        inTemplateArgs_ = wasInArgs;
        return;
    }
    // A pack took the rest of the list: a pack after it is empty, and anything else is left to deduction.
    while (binding->size() < decl.params.size() && decl.params[binding->size()].isPack) {
        TemplateArg empty;
        empty.isPack = true;
        empty.packIsValue = decl.params[binding->size()].type != nullptr;
        if (empty.packIsValue) empty.type = paramTypeIn(decl.params[binding->size()], *binding);
        binding->push_back(nullptr);
        values->push_back(0);
        args->push_back(empty);
    }
    if (binding->size() < decl.params.size())
        src_.fail(peek().pos, "'" + decl.name + "' has a parameter after the pack '" +
                              decl.params[binding->size() - 1].name + "', and deducing it from the "
                              "call once the pack's arguments are written is not supported yet");
    takeClosingAngle();
    inTemplateArgs_ = wasInArgs;
}

// **A type as a specialization's key spells it**: describe(), but an enum by
// its own name, not its underlying type - `k<E>` and `k<int>` were one
// specialization, and an enum argument called the one for int.
// Whether an enumeration is written anywhere in a type - the part describe() spells as its underlying type.
static bool mentionsEnumeration(const Type *t) {
    const Type *u = t->unqualified();
    if (u->isEnumeration()) return true;
    if (u->isFunction()) {
        if (mentionsEnumeration(u->returns())) return true;
        for (const Type *p : u->params())
            if (mentionsEnumeration(p)) return true;
        return false;
    }
    if (u->kind() == Kind::Pointer || u->kind() == Kind::LValueRef || u->kind() == Kind::RValueRef || u->isArray())
        return mentionsEnumeration(u->pointee());
    return false;
}

static std::string keySpelling(const Type *t) {
    const Type *u = t->unqualified();
    std::string s;
    if (u->isEnumeration()) s = u->enumTag();
    // **A function type that names an enumeration is keyed by it** (1.81): describe() spells it as its
    // underlying type, and `Name<void(Color)>` was `Name<void(int)>` - a specialization for the one was taken by
    // the other, and `Signal<void(State, Event)>` and `Signal<void(State, State)>` became one class.
    else if (u->isFunction() && mentionsEnumeration(u)) {
        s = keySpelling(u->returns()) + "(";
        for (std::size_t i = 0; i < u->params().size(); i++) s += (i > 0 ? "," : "") + keySpelling(u->params()[i]);
        if (u->isVariadicFn()) s += u->params().empty() ? "..." : ",...";
        s += ")";
        if (u->cvThis() & Type::kThisConst) s += " const";
        if (u->cvThis() & Type::kThisVolatile) s += " volatile";
        if (u->refQualifier() == RefQual::LValue) s += " &";
        if (u->refQualifier() == RefQual::RValue) s += " &&";
    }
    else if (u->isArray() && mentionsEnumeration(u))
        s = keySpelling(u->pointee()) + "[" + std::to_string(u->length()) + "]";
    else if (u->kind() == Kind::Pointer && (!u->pointee()->isFunction() || mentionsEnumeration(u)))
        s = keySpelling(u->pointee()) + " *";
    else if (u->kind() == Kind::LValueRef && !u->pointee()->isArray()) s = keySpelling(u->pointee()) + " &";
    else if (u->kind() == Kind::RValueRef && !u->pointee()->isArray()) s = keySpelling(u->pointee()) + " &&";
    else return t->describe();
    if (t->isVolatile()) s = u->kind() == Kind::Pointer ? s + " volatile" : "volatile " + s;
    if (t->isConst()) s = u->kind() == Kind::Pointer ? s + " const" : "const " + s;
    return s;
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
                // A pattern's `Ns...` is keyed as the expansion, apart from a pack of the one type `T`.
                if (args[i].expandsParam >= 0 && k + 1 == args[i].pack.size())
                    key += "$T" + std::to_string(args[i].expandsParam) + "...";
                else if (args[i].packIsValue && args[i].pack[k]->kind() == Kind::TemplateParam)
                    key += "$P" + std::to_string(args[i].pack[k]->length());
                else if (args[i].packIsValue) key += std::to_string(args[i].packValues[k]);
                else if (args[i].pack[k]->kind() == Kind::TemplateParam)
                    key += "$T" + std::to_string(args[i].pack[k]->length()) + (args[i].pack[k]->isConst() ? "$c" : "") +
                           (args[i].pack[k]->isVolatile() ? "$v" : "");
                else key += keySpelling(args[i].pack[k]);
            }
            key += "}";
        }
        else if (args[i].expr != nullptr) key += "$X" + args[i].expr->key;
        else if (args[i].isParam)
            // A non-type parameter reference - `V<N>` in a pattern - keyed by
            // which parameter, or `V<N>` and `V<M>` would be one type.
            key += "$P" + std::to_string(args[i].paramIndex);
        // An address by what it names, so `call<fn>` and `call<&fn>` are one specialization.
        else if (args[i].form != 0) key += args[i].address;
        else if (!args[i].isType) key += std::to_string(args[i].value);
        // A pattern's argument is a template parameter, and describe() would
        // put a space in a tag every table is keyed by.
        // Its cv too: `pair<const T1, T2>` and `pair<T1, T2>` are two patterns (1.82: they were one key, and
        // whichever was read first stood for both - `std::map<K, V> &` then deduced K as `const K`'s).
        else if (args[i].type->kind() == Kind::TemplateParam)
            key += "$T" + std::to_string(args[i].type->length()) + (args[i].type->isConst() ? "$c" : "") +
                   (args[i].type->isVolatile() ? "$v" : "");
        // A template named as an argument: `Holder<Box,int>`, the name as written.
        else if (args[i].isTemplate)
            key += args[i].type->templateNamespace() + args[i].type->templateName();
        else key += keySpelling(args[i].type);
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
                    const Packs &packs) {
    // A specialization of a template only declared so far is a declaration; a definition later in
    // the file is read for it then ([temp.point]), and none at all is the linker's to report.
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
    // And which template: two differing only in `decltype(...)` returns were one key, the first.
    key += "@" + std::to_string(decl.afterParams);

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

    // A `static` template has internal linkage, which Itanium writes as an L: `_ZL5near1...`.
    bool internal = false;
    for (std::size_t i = decl.afterParams; i < tokens_.size() && !tokens_[i].is("(") &&
                                            tokens_[i].text != decl.name; i++)
        internal = internal || tokens_[i].is("static");
    std::string symbol, why;
    const bool ok = target_.microsoftNames()
        ? microsoftTemplateFunctionName(decl.ns + decl.name, fn, args, &symbol, &why)
        : itaniumTemplateFunctionName(decl.ns + decl.name, patternFn, declaredArguments(decl.params, args),
                                      internal, &symbol, &why);
    if (!ok)
        src_.fail(pos, "'" + key + "' cannot be given a name the linker can "
                       "hold: " + why);

    Specialization sp;
    sp.key = key;
    sp.name = decl.name;
    sp.templateKey = tagName(decl);
    sp.display = decl.ns + display;
    sp.params = decl.params;
    sp.packs = packs;
    sp.symbol = symbol;
    sp.fn = fn;
    sp.binding = binding;
    sp.values = values;
    sp.start = decl.afterParams;
    sp.ns = decl.ns;
    sp.ownNamespace = true;
    if (!decl.friendOf.empty()) sp.friendDecl = std::make_shared<TemplateDecl>(decl);
    sp.pos = pos;
    sp.chain = src_.instantiations();
    sp.depth = instantiationDepth_ + 1;
    refuseInstantiationDepth(decl.name, sp.depth, pos);
    specializations_.push_back(sp);

    // **Under two keys, on purpose.** "twice<int>" is what the replayed definition
    // declares and what a repeat of the same arguments finds; "twice" is what
    // overload resolution must see, a specialization competing with the ordinary.
    const std::size_t at = functions_.size();
    functionIndex_[key].push_back(at);
    functionIndex_[decl.name].push_back(at);
    // And under the template's namespace, where that namespace's ordinary functions of the name
    // are: a template and a non-template there compete as one set, and the non-template wins a tie.
    // Where that namespace is inline, under the enclosing one's as well - `lib::twice(1)`.
    if (!decl.ns.empty()) functionIndex_[decl.ns + decl.name].push_back(at);
    else functionIndex_["::" + decl.name].push_back(at);   // the global namespace's own
    {
        const std::vector<std::string> alts = inlineAliasPrefixes(decl.ns);
        for (std::size_t a = 0; a < alts.size(); a++) functionIndex_[alts[a] + decl.name].push_back(at);
    }
    functions_.push_back(Signature{ display, symbol, fn->returns(), fn->params(),
                                    fn->isVariadicFn(), false, pos, false,
                                    std::string(), false, Access::Public });
    functions_.back().fromTemplate = true;
    functions_.back().pattern = patternFn;
    functions_.back().isNoexcept = patternNoexcept_;   // the pattern's bare `noexcept`, read above (1.27)
    functions_.back().deleted = decl.isDeleted;
    // **The defaults the pattern's parameter list just read**, and the arguments they are read with.
    requireDefaultsAreASuffix(pendingDefaults_, pendingDefaultsAt_);
    if (!pendingDefaults_.empty()) {
        defaultArgs_[symbol] = pendingDefaults_;
        // Read where the template was written - `flags = regex_constants::match_default` names std's - not where it
        // was called from.
        defaultArgNamespace_[symbol] = decl.ns.empty() ? namespaceStack_ : namespacePathStack(decl.ns);
        defaultArgBindings_[symbol] = DefaultBinding{ decl.params, binding, values, packs };
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

bool Parser::carryTrialUses() {
    bool carried = false;
    for (std::size_t t = 0; t < trialUses_.size(); ) {
        bool real = false;
        for (std::size_t i : trialUses_[t].made)
            real = real || (i < functions_.size() && functions_[i].used);
        if (!real) { t++; continue; }
        for (std::size_t i : trialUses_[t].called)
            if (i < functions_.size() && !functions_[i].used) {
                functions_[i].used = true;
                carried = true;
            }
        trialUses_.erase(trialUses_.begin() + static_cast<std::ptrdiff_t>(t));
    }
    return carried;
}

void Parser::instantiatePending() {
    for (bool again = true; again; ) {
        again = false;
        carryTrialUses();
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
                const TemplateDecl &d = templates_[specializations_[i].templateKey];
                std::vector<bool> &done = specializations_[i].outsideDone;
                done.resize(d.outOfLine.size(), false);
                for (std::size_t k = 0; k < d.outOfLine.size(); k++) {
                    if (done[k] || specializations_[i].fromPartial) continue;
                    const TemplateDecl::OutOfLine &o = d.outOfLine[k];
                    // The class the member is in: the specialization, or a class nested in it.
                    const std::string tag = specializations_[i].key + (o.nested.empty() ? "" : "::" + o.nested);
                    if (!o.nested.empty()) {
                        const auto lazy = lazyMemberClasses_.find(specializations_[i].key + "::" +
                                                                  o.nested.substr(0, o.nested.find("::")));
                        if (lazy != lazyMemberClasses_.end() && lazy->second.replaced) { done[k] = true; continue; }
                    }
                    if (o.constructor || o.destructor) {
                        if (!memberIsUsed(o.constructor ? constructorKey(tag) : destructorKey(tag))) continue;
                        done[k] = true;
                        PendingBody b;
                        b.tag = tag; b.start = o.start; b.local = o.member; b.which = PendingBody::npos();
                        b.key = o.constructor ? constructorKey(tag) : destructorKey(tag);
                        now.push_back(b);
                        continue;
                    }
                    // **A static data member is defined only where an evaluated
                    // expression named it**, [temp.inst]/3, as clang emits it.
                    if (d.outOfLine[k].isData) {
                        const Type *spec = findTypedef(tag);
                        // A member class only declared ([temp.inst]/1) has no member to define yet.
                        if (spec != nullptr && !spec->unqualified()->isComplete()) continue;
                        const Type::StaticMember *sm =
                            spec != nullptr ? spec->findStaticMember(d.outOfLine[k].member)
                                            : nullptr;
                        if (sm != nullptr && !usedStaticMembers_.count(sm->symbol)) continue;
                    } else if (!memberIsUsed(tag + "::" + d.outOfLine[k].member)) continue;
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
            TemplateDecl decl = sp.friendDecl ? *sp.friendDecl : templates_[sp.templateKey];
            if (sp.ownNamespace) decl.ns = sp.ns;

            std::vector<Shadow> undo;
            // A friend template of a class template: the class's parameters first, its own over them.
            if (sp.friendDecl && !decl.classParams.empty())
                bindTemplateParameters(decl.classParams, decl.classBinding, decl.classValues, decl.classPacks, &undo);
            bindTemplateParameters(sp.params, sp.binding, sp.values, sp.packs, &undo);
            const std::string wasKey = instantiationKey_;
            const std::string wasOf = instantiationOf_;
            const std::string wasDisplay = instantiationDisplay_;
            instantiationKey_ = sp.isClass ? std::string() : sp.key;
            instantiationOf_ = sp.isClass ? std::string() : sp.name;
            instantiationDisplay_ = sp.isClass ? std::string() : sp.display;
            // What this body asks for is one deeper than it - and a specialization appended
            // while this round runs is replayed in this same round, so a chain that never
            // settles is one round long; the depth, not the round, is what bounds it.
            const int wasDepth = instantiationDepth_;
            instantiationDepth_ = sp.depth;
            const ChainOf asked(src_, sp.chain);
            const InChain chain(src_, sp.pos, sp.display.empty() ? sp.key.substr(0, sp.key.find_first_of("@#")) : sp.display);

            const std::size_t resume = at_;
            // **An instantiated definition has vague linkage** - [basic.link] and [temp.spec]: the
            // same specialization may be produced by every translation unit that uses it, and the
            // linker folds the copies rather than rejecting them.
            const bool wasReplayingInline = replayingInline_;
            replayingInline_ = true;
            // Its bodies read names in the namespace it was declared in, wherever it is used from.
            const std::vector<std::string> wasStack = namespaceStack_;
            if (!decl.ns.empty()) namespaceStack_ = namespacePathStack(decl.ns);
            // **Not a member template's body, though one may have asked for it**: `s.size()` inside
            // `bitset<8>::bitset<char>` defines basic_string<char>'s ordinary size(), which would match
            // no declaration were it read as the member template's specialization.
            struct NotMemberTemplate {
                Parser *p; bool inst, body;
                ~NotMemberTemplate() { p->memberTemplateInst_ = inst; p->memberTemplateBody_ = body; }
            } notMember{ this, memberTemplateInst_, memberTemplateBody_ };
            memberTemplateInst_ = false;
            memberTemplateBody_ = false;
            if (sp.isClass) {
                // Its member functions, held here; inlineOwner_ gives the scope.
                replayInlineBodies(now);
                // And the ones written outside it, which need no owner: the
                // tokens say `Box<T>::get`, so with T bound the ordinary
                // member-definition path reads the qualifier itself.
                for (std::size_t k = 0; k < outsideNow.size(); k++) {
                    at_ = templates_[sp.templateKey].outOfLine[outsideNow[k]].start;
                    topLevel(*current_);
                }
            } else if (sp.friendDecl) {
                // Read in its class, as a friend's body written there is (Parser::replayInlineBodies).
                const Type *scope = findTypedef(decl.friendOf);
                struct InClass {
                    Parser *p; bool pushed; const Type *was; bool wasFriend;
                    ~InClass() {
                        if (pushed) p->classStack_.pop_back();
                        p->friendScope_ = was;
                        p->friendDeclaration_ = wasFriend;
                    }
                } inClass{ this, scope != nullptr, friendScope_, friendDeclaration_ };
                if (scope != nullptr) classStack_.push_back(scope);
                friendScope_ = scope != nullptr && scope->isStructOrUnion() ? scope->unqualified() : nullptr;
                friendDeclaration_ = true;
                at_ = sp.start;
                topLevel(*current_);
            } else {
                at_ = sp.start;
                topLevel(*current_);
            }
            namespaceStack_ = wasStack;
            replayingInline_ = wasReplayingInline;
            at_ = resume;

            instantiationKey_ = wasKey;
            instantiationOf_ = wasOf;
            instantiationDisplay_ = wasDisplay;
            instantiationDepth_ = wasDepth;
            unbindTemplateParameters(undo);
        }
        if (carryTrialUses()) again = true;
    }
}

// **A chain of instantiations that never settles** (f<T> asking f<T *>) is
// refused kMaxNesting bodies deep, as cl and clang do; each specialization
// carries its depth, and the error reaches the user, not as a failed match.
void Parser::refuseInstantiationDepth(const std::string &name, int depth, std::size_t pos) const {
    if (depth <= kMaxNesting) return;
    src_.fail(pos, "'" + name + "' is being instantiated from inside its own instantiations more than " +
                   std::to_string(kMaxNesting) + " levels deep, and the chain has not ended yet - a "
                   "template that asks for a new specialization of itself each time, with an argument "
                   "that grows, never settles, and the compiler stops here rather than run out of memory");
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
    // The qualifier first: a `const auto` is a const copy of the deduced kind, and asking the kind
    // first answered `with` bare - `const auto e = i;` was an `int` that could be assigned to, and
    // `const auto &c = gc;` an `int &` that could not bind (1.27, 2026-10-21, review of 1.25, item 3).
    if (t->unqualified() != t)
        return types_.withConst(substituteDeduced(t->unqualified(), with));
    if (t->kind() == Kind::Deduced) return with;
    if (t->isPointer()) return types_.pointerTo(substituteDeduced(t->pointee(), with));
    if (t->isReference()) {
        const Type *r = substituteDeduced(t->referent(), with);
        if (r->isReference()) return t->isRValueReference() ? r : types_.referenceTo(r->referent());
        return t->isRValueReference() ? types_.rvalueReferenceTo(r) : types_.referenceTo(r);
    }
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
    // [dcl.spec.auto]/3: in scope there, it has no type until the initialiser gives one.
    if (initialiserNames(name))
        src_.fail(pos, "'" + name + "' is declared 'auto' and names itself in its "
                       "own initialiser - its type is what that initialiser "
                       "decides, so there is none to use");

    const std::size_t resume = at_;
    if (paren) at_++;
    else if (!consume("=") && peek().is("{"))
        src_.fail(peek().pos, "'auto " + name + "{...}' deduces 'std::initializer_list' by "
                              "C++11's words and the element's type by N3922's, which "
                              "compilers apply to C++11 - not supported yet; write '= {...}' "
                              "for the list, or '= ...' for the one value");
    if (peek().is("{")) {
        const Type *il = deduceBracedAuto(name, pos);
        at_ = resume;
        // `const auto &r = {...}` binds the list's temporary, [dcl.init.list]/3; a pointer deduces nothing.
        const Type *plain = declared->isReference() ? declared->referent() : declared;
        if (plain->kind() != Kind::Deduced &&
            !(plain->unqualified() != plain && plain->unqualified()->kind() == Kind::Deduced))
            src_.fail(pos, "'" + name + "' is declared '" + declared->describe() + "' from a braced "
                           "list, and only 'auto', 'const auto' or a reference to them deduce "
                           "'std::initializer_list' from one - [dcl.spec.auto]/6");
        return deduceAutoFrom(declared, il, name, pos);
    }
    // **Read for its type and rewound**, so this reading built nothing: the
    // real one happens below, from the same tokens.
    const Type *from = nullptr;
    {
        Discarded held(this);
        ExprPtr init = assign();
        from = deductionArgType(*init);
        // [temp.deduct.call]/2-3 for a reference: a function is not decayed, so `auto &rf = fn;` deduces
        // `void(int)` and declares `void (&)(int)` (R22: it was `void (*)(int) &`).
        if (declared->isReference())
            if (const Type *fn = functionDesignated(*init)) from = fn;
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

// An lvalue argument travels to deduction as `U &`: an expression's type never carries
// its value category, and a forwarding reference is the one parameter that asks for it.
const Type *Parser::deductionArgType(const Expr &e) const {
    if (const OverloadArg *oa = overloadArgOf(e))
        src_.fail(oa->pos, "'" + oa->key + "' names " + std::to_string(oa->set.size()) +
                           " functions, and is passed where a template deduces from it - choosing "
                           "one there is not supported yet; initialise a pointer of the type wanted "
                           "and pass that");
    // **A braced list travels as an array of its elements' one type with no reference** - `{1, 2}` as
    // `int[2]` - which no other argument does (an lvalue array arrives as `U (&)[n]`): deduceFromArgument
    // reads initializer_list<P'> and an array reference from it, and nothing else ([temp.deduct.call]/1).
    if (const Init *in = bracedInitOf(e)) {
        const Type *elem = nullptr;
        for (std::size_t i = 0; i < in->items.size(); i++) {
            if (in->items[i].isList || in->items[i].value == nullptr || in->items[i].value->type() == nullptr)
                return e.type();
            const Type *one = decayedType(in->items[i].value->type());
            if (elem != nullptr && one != elem) return e.type();
            elem = one;
        }
        if (elem == nullptr) return e.type();
        return types_.arrayOf(elem, static_cast<long long>(in->items.size()));
    }
    const Type *t = e.type();
    // A string literal is an lvalue too, [expr.prim.general]/1: `fs("lit")` with `T &&` deduces T as
    // `const char (&)[4]`, which std::forward passes on as the array it is (R15).
    const bool literal = dynamic_cast<const StrLit *>(&e) != nullptr && t->isArray();
    // A class temporary and `&S::f` live in a slot and are marked prvalues: `T &&` deduces their type itself.
    const bool lvalue = e.declCategory() != Expr::Category::Prvalue && isLvalue(e);
    if ((lvalue || literal) && !t->isReference() && !t->isFunction()) return types_.referenceTo(t);
    return t;
}

// The argument with the pattern's own cv taken off: `const T` given a `const volatile int` leaves T a
// `volatile int`, where unqualified() left it `int`. An array's cv is its elements' and comes off whole.
const Type *Parser::withoutCvOf(const Type *arg, const Type *pattern) const {
    // An array's cv is its elements': `const T &` against `const char[3]` is T = `char[3]`, [temp.deduct.call]/3.
    if (arg->isArray()) {
        const Type *a = arg->unqualified();
        if (pattern->isConst()) a = types_.withoutConst(a);
        if (pattern->isVolatile()) a = types_.withoutVolatile(a);
        return a;
    }
    return types_.qualified(arg, arg->isConst() && !pattern->isConst(), arg->isVolatile() && !pattern->isVolatile());
}

// `T &&` with T a bare template parameter, or `auto &&` ([dcl.spec.auto]/7 deduces it the same way).
bool Parser::forwardingReference(const Type *pattern) {
    if (!pattern->isRValueReference()) return false;
    const Type *t = pattern->referent();
    return (t->kind() == Kind::TemplateParam || t->kind() == Kind::Deduced) && t->unqualified() == t;
}

// One parameter of the pattern against one argument's type. The pattern still has
// Kind::TemplateParam in it, so "does this position deduce anything" is a question
// about the type and not about a table: one reached here binds.
bool Parser::deduceOne(const Type *pattern, const Type *arg,
                       std::vector<const Type *> *binding,
                       std::vector<long long> *values,
                       std::string *why, Packs *packs, bool nested) const {
    // **Inside a template-id the argument is matched as it is**
    // ([temp.deduct.type]/8): `It<C, V>` against `It<S, const int>` makes
    // V `const int`; the call rules below (decay, cv dropped) do not apply.
    if (nested) {
        // The pattern's own cv first: `const K` in `pair<const K, V>` against `const int` makes K `int` (1.82: a
        // const-qualified parameter is still of its kind, and K was bound `const int` - std::map<K, V> & did not
        // deduce, its defaulted `allocator<pair<const K, V>>` disagreeing with the `K` written).
        if (pattern->unqualified() != pattern &&
            (pattern->unqualified()->kind() == Kind::TemplateParam || pattern->unqualified()->kind() == Kind::Deduced)) {
            if ((pattern->isConst() && !arg->isConst()) || (pattern->isVolatile() && !arg->isVolatile())) {
                *why = "'" + arg->describe() + "' is not " + (pattern->isConst() && !arg->isConst() ? "const" : "volatile") +
                       " where '" + pattern->describe() + "' is";
                return false;
            }
            return deduceOne(pattern->unqualified(), withoutCvOf(arg, pattern), binding, values, why, packs, true);
        }
        if (pattern->kind() == Kind::TemplateParam || pattern->kind() == Kind::Deduced) {
            const std::size_t i = pattern->kind() == Kind::Deduced
                                      ? 0 : static_cast<std::size_t>(pattern->length());
            if ((*binding)[i] == nullptr) { (*binding)[i] = arg; return true; }
            if ((*binding)[i] != arg) {
                *why = "it is '" + (*binding)[i]->describe() + "' in one argument "
                       "and '" + arg->describe() + "' in another";
                return false;
            }
            return true;
        }
        if (pattern->unqualified() != pattern) {
            if ((pattern->isConst() && !arg->isConst()) || (pattern->isVolatile() && !arg->isVolatile())) {
                *why = "'" + arg->describe() + "' is not " + (pattern->isConst() && !arg->isConst() ? "const" : "volatile") +
                       " where '" + pattern->describe() + "' is";
                return false;
            }
            return deduceOne(pattern->unqualified(), withoutCvOf(arg, pattern), binding, values, why, packs, true);
        }
    }
    // **[temp.deduct.call]/3: `T &&` given an lvalue of type U deduces T as `U &`**, and
    // [dcl.ref]/6 then collapses the parameter to `U &` when the declaration is re-read.
    if (!nested && forwardingReference(pattern) && arg->isReference() && !arg->isRValueReference()) {
        const Type *param = pattern->referent();
        const std::size_t i = param->kind() == Kind::Deduced ? 0 : static_cast<std::size_t>(param->length());
        const Type *deduced = types_.referenceTo(arg->referent());
        if ((*binding)[i] == nullptr) { (*binding)[i] = deduced; return true; }
        if ((*binding)[i] != deduced) {
            *why = "it is '" + (*binding)[i]->describe() + "' in one argument "
                   "and '" + deduced->describe() + "' in another";
            return false;
        }
        return true;
    }
    // **A reference parameter looks through itself; others decay.** `const
    // T &` given int makes T int; `T &` or `auto &` with T unqualified keeps
    // the argument's cv in T, [temp.deduct.call]/3 (`const int` stays).
    bool keepsQualifiers = false;
    if (nested) {
        // matched as it is - see the top
    } else if (pattern->isReference()) {
        pattern = pattern->referent();
        if (arg->isReference()) arg = arg->referent();
        // What the pattern's own cv leaves of the argument's is T's: `const T &` given a `const
        // volatile int` deduces T as `volatile int`.
        if (pattern->unqualified() != pattern) {
            arg = withoutCvOf(arg, pattern);
            pattern = pattern->unqualified();
        }
        keepsQualifiers = true;
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
        const Type *deduced = keepsQualifiers ? arg : arg->unqualified();
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
        int tt;
        // **[temp.deduct.type]/8's `TT<T>`**: a template template parameter deduces from the argument's own template, then the arguments pairwise.
        const bool viaParam = ttRefName(pattern->templateName(), &tt);
        // **`C<T...>` against `vector<int, allocator<int>>`** ([temp.deduct.type]/9): the pattern's one pack
        // takes every argument of a template that lists its own one by one - held here as the pack the
        // pattern expects (1.79: only a variadic template's specialization matched, "is not a template's
        // specialization").
        std::vector<TemplateArg> folded;
        const std::vector<TemplateArg> *given = arg->isSpecialization() ? &arg->templateArgs() : nullptr;
        if (viaParam && given != nullptr && pattern->templateArgs().size() == 1 &&
            pattern->templateArgs()[0].isType && pattern->templateArgs()[0].isPack &&
            pattern->templateArgs()[0].expandsParam >= 0 && !pattern->templateArgs()[0].packIsValue &&
            !(given->size() == 1 && (*given)[0].isPack)) {
            bool allTypes = true;
            TemplateArg one;
            one.isType = true;
            one.isPack = true;
            for (const TemplateArg &a : *given) {
                if (!a.isType || a.isPack || a.isTemplate || a.type == nullptr) { allTypes = false; break; }
                one.pack.push_back(a.type);
            }
            if (allTypes) { folded.push_back(one); given = &folded; }
        }
        if (!arg->isSpecialization() ||
            (!viaParam && arg->templateName() != pattern->templateName()) ||
            given->size() != pattern->templateArgs().size()) {
            *why = "'" + arg->describe() + "' is not a '" +
                   (viaParam ? "template's specialization" : pattern->templateName()) + "'";
            return false;
        }
        if (viaParam) {
            const Type *deduced = templateOf(arg);
            const std::size_t i = static_cast<std::size_t>(tt);
            if ((*binding)[i] == nullptr) (*binding)[i] = deduced;
            else if ((*binding)[i] != deduced) {
                *why = "it is '" + (*binding)[i]->describe() + "' in one argument and '" +
                       deduced->describe() + "' in another";
                return false;
            }
        }
        for (std::size_t i = 0; i < pattern->templateArgs().size(); i++) {
            const TemplateArg &p = pattern->templateArgs()[i];
            const TemplateArg &a = (*given)[i];
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
            if (p.isTemplate != a.isTemplate || p.isPack != a.isPack) {
                *why = "'" + arg->describe() + "' gives a " +
                       (a.isPack ? "pack" : a.isTemplate ? "template" : "type") +
                       " where the pattern has a " + (p.isPack ? "pack" : p.isTemplate ? "template" : "type");
                return false;
            }
            // `C<int>` for a `template <class...> class C` holds its arguments as one pack, as `Tup<int>` does.
            if (p.isPack) {
                // **`List<Ns...>` against `List<1, 2, 3>`**: the pack takes every member, [temp.deduct.type]/9;
                // `Tup<H, T...>` matches H to the first and T to the rest.
                if (p.expandsParam >= 0) {
                    if (packs == nullptr || static_cast<std::size_t>(p.expandsParam) >= packs->size()) {
                        *why = "a parameter pack cannot be deduced here";
                        return false;
                    }
                    if (p.packIsValue != a.packIsValue) {
                        *why = "'" + arg->describe() + "' gives " + (a.packIsValue ? "values" : "types") +
                               " where the pattern's pack takes " + (p.packIsValue ? "values" : "types");
                        return false;
                    }
                    const std::size_t fixed = p.pack.size() - 1;
                    if (a.pack.size() < fixed) {
                        *why = "'" + arg->describe() + "' has fewer arguments than the pattern";
                        return false;
                    }
                    for (std::size_t k = 0; k < fixed; k++) {
                        if (!p.packIsValue) {
                            if (!deduceOne(p.pack[k], a.pack[k], binding, values, why, packs, true)) return false;
                            continue;
                        }
                        if (p.pack[k]->kind() == Kind::TemplateParam) {
                            const std::size_t j = static_cast<std::size_t>(p.pack[k]->length());
                            if (j < binding->size()) {
                                (*binding)[j] = a.pack[k];
                                if (values != nullptr) (*values)[j] = a.packValues[k];
                            }
                        } else if (p.packValues[k] != a.packValues[k]) {
                            *why = "'" + arg->describe() + "' does not give the pattern's values";
                            return false;
                        }
                    }
                    PackMembers &out = (*packs)[static_cast<std::size_t>(p.expandsParam)];
                    PackMembers got(std::vector<const Type *>(a.pack.begin() + static_cast<long>(fixed), a.pack.end()));
                    if (a.packIsValue)
                        got.values.assign(a.packValues.begin() + static_cast<long>(fixed), a.packValues.end());
                    const bool bound = binding != nullptr && (*binding)[static_cast<std::size_t>(p.expandsParam)] != nullptr;
                    if (bound && (static_cast<const std::vector<const Type *> &>(out) != got ||
                                  out.values != got.values)) {
                        *why = "the parameter pack is deduced two different lists";
                        return false;
                    }
                    out = got;
                    (*binding)[static_cast<std::size_t>(p.expandsParam)] = p.pack.back();
                    continue;
                }
                if (p.pack.size() != a.pack.size()) {
                    *why = "'" + arg->describe() + "' has a different number of arguments";
                    return false;
                }
                if (p.packIsValue) {
                    if (!a.packIsValue) {
                        *why = "'" + arg->describe() + "' gives types where the pattern has values";
                        return false;
                    }
                    for (std::size_t k = 0; k < p.pack.size(); k++) {
                        // `List<M>` in the pattern: M is deduced the member's value, as `V<N>` deduces N.
                        if (p.pack[k]->kind() == Kind::TemplateParam) {
                            const std::size_t j = static_cast<std::size_t>(p.pack[k]->length());
                            if (j >= binding->size()) continue;
                            if ((*binding)[j] != nullptr && values != nullptr && (*values)[j] != a.packValues[k]) {
                                *why = "a non-type parameter is deduced two different values";
                                return false;
                            }
                            (*binding)[j] = a.pack[k];
                            if (values != nullptr) (*values)[j] = a.packValues[k];
                        } else if (p.packValues[k] != a.packValues[k]) {
                            *why = "'" + arg->describe() + "' does not give the pattern's values";
                            return false;
                        }
                    }
                    continue;
                }
                for (std::size_t k = 0; k < p.pack.size(); k++)
                    if (!deduceOne(p.pack[k], a.pack[k], binding, values, why, packs, true)) return false;
                continue;
            }
            // [temp.deduct.type]/8: an argument the pattern writes out is the argument's exactly - `basic_ostream<char,
            // Tr>` is no `basic_ostream<wchar_t, ...>`, and nothing is deduced or made from it.
            if (!dependentType(p.type) && p.type != a.type && p.type->describe() != a.type->describe()) {
                *why = "'" + arg->describe() + "' is not a '" + pattern->describe() + "'";
                return false;
            }
            if (!deduceOne(p.type, a.type, binding, values, why, packs, true)) return false;
        }
        return true;
    }

    // **What a pointer or array holds is matched as it is**, its cv part of
    // T unless the pattern names it: `T *` given `const int *` makes T
    // `const int`, `const T *` given `int *` T `int` ([temp.deduct.call]/4).
    if (pattern->isPointer() && arg->isPointer()) {
        const Type *pp = pattern->pointee(), *ap = arg->pointee();
        if (pp->unqualified() != pp) { ap = withoutCvOf(ap, pp); pp = pp->unqualified(); }
        if (pp->kind() == Kind::TemplateParam || pp->kind() == Kind::Deduced)
            return deduceOne(pp, ap, binding, values, why, packs, true);
        // A function pointee is matched as is: decayed, R (*)(A) misses it.
        return deduceOne(pp, ap, binding, values, why, packs, nested || pp->isFunction());
    }
    if (pattern->isArray() && arg->isArray()) {
        // **`T (&)[N]` against an `int[3]` gives N=3**, [temp.deduct.type]/8's `type[i]`; the binding
        // marks N deduced and the value is what is read. Every level here: an element does not decay,
        // and recursing would decay `int[4]` in `T (&)[M][N]` to an `int *`.
        while (pattern->isArray() && arg->isArray()) {
            const int j = pattern->boundParam();
            if (j >= 0 && static_cast<std::size_t>(j) < binding->size()) {
                if (arg->length() < 0) {
                    *why = "'" + arg->describe() + "' has no length to give a "
                           "non-type parameter";
                    return false;
                }
                if ((*binding)[j] != nullptr && values != nullptr &&
                    (*values)[j] != arg->length()) {
                    *why = "a non-type parameter is deduced two different values";
                    return false;
                }
                (*binding)[j] = types_.intType();
                if (values != nullptr) (*values)[j] = arg->length();
            }
            pattern = pattern->pointee();
            arg = arg->pointee();
        }
        if (pattern->isArray() || arg->isArray()) return true;
        if (pattern->unqualified() != pattern) { pattern = pattern->unqualified(); arg = arg->unqualified(); }
        if (pattern->kind() == Kind::TemplateParam || pattern->kind() == Kind::Deduced)
            return deduceOne(pattern, arg, binding, values, why, packs, true);
        return deduceOne(pattern, arg, binding, values, why, packs);
    }
    // **`R (*)(A)` against a function pointer** - [temp.deduct.type]/8: the
    // return type and each parameter as they are; a trailing `Ts...` takes
    // every parameter left, [temp.deduct.type]/10.
    if (pattern->isFunction() && arg->isFunction()) {
        const std::vector<const Type *> &pp = pattern->params(), &ap = arg->params();
        const bool tail = !pp.empty() && pp.back()->kind() == Kind::PackExpansion &&
                          pp.back()->pointee()->kind() == Kind::TemplateParam && packs != nullptr &&
                          static_cast<std::size_t>(pp.back()->pointee()->length()) < packs->size();
        const std::size_t fixed = tail ? pp.size() - 1 : pp.size();
        if ((tail ? ap.size() < fixed : ap.size() != fixed) || pattern->isVariadicFn() != arg->isVariadicFn()) {
            *why = "'" + arg->describe() + "' takes a different number of parameters from '" +
                   pattern->describe() + "'";
            return false;
        }
        if (!deduceOne(pattern->returns(), arg->returns(), binding, values, why, packs, true)) return false;
        for (std::size_t i = 0; i < fixed; i++)
            if (!deduceOne(pp[i], ap[i], binding, values, why, packs, true)) return false;
        if (tail) {
            const std::size_t which = static_cast<std::size_t>(pp.back()->pointee()->length());
            const PackMembers got(std::vector<const Type *>(ap.begin() + static_cast<long>(fixed), ap.end()));
            const bool bound = binding != nullptr && which < binding->size() && (*binding)[which] != nullptr;
            if (bound && static_cast<const std::vector<const Type *> &>((*packs)[which]) != got) {
                *why = "the parameter pack is deduced two different lists";
                return false;
            }
            (*packs)[which] = got;
            if (binding != nullptr && which < binding->size()) (*binding)[which] = pp.back();
        }
        return true;
    }
    // `T S::*` against `int S::*`: the member's type, and the class when it is
    // a parameter too.
    if (pattern->isMemberPointer() && arg->isMemberPointer())
        return deduceOne(pattern->pointee(), arg->pointee(), binding, values, why, packs) &&
               deduceOne(pattern->enclosing(), arg->enclosing(), binding, values, why, packs);
    // `R (C::*)(A) const` against a pointer to a member function: the function type as it is, cv and all -
    // not decayed to a pointer, which the function branch above would not match.
    if (pattern->unqualified()->isMemberFunctionPointer() && arg->unqualified()->isMemberFunctionPointer()) {
        const Type *pf = pattern->unqualified()->pointee(), *af = arg->unqualified()->pointee();
        if (pf->cvThis() != af->cvThis() || pf->refQualifier() != af->refQualifier()) {
            *why = "'" + arg->describe() + "' is not qualified as '" + pattern->describe() + "' is";
            return false;
        }
        return deduceOne(pf, af, binding, values, why, packs, true) &&
               deduceOne(pattern->unqualified()->enclosing(), arg->unqualified()->enclosing(),
                         binding, values, why, packs, true);
    }

    return true;  // nothing to deduce; overload resolution ranks a mismatch
}

// Every base of `cls` that is a specialization of `name`, at any depth - of any template for a pattern's `C<T>`.
static void basesNamed(const Type *cls, const std::string &name, std::vector<const Type *> *out) {
    int tt;
    for (const Type::BaseSpec &b : cls->unqualified()->bases()) {
        const Type *t = b.type->unqualified();
        if (t->isSpecialization() && (t->templateName() == name || ttRefName(name, &tt))) out->push_back(t);
        basesNamed(t, name, out);
    }
}

// **[temp.deduct.call]/4.3: a parameter `V<K>`, `V<K> &` or `V<K> *` deduces from a base**
// of the argument's class when the class itself is no `V` - `q * r` for `Q<4> : V<4>` gives
// K = 4. Only at the parameter's top, and only where exactly one base deduces.
bool Parser::deduceFromArgument(const Type *param, const Type *arg,
                                std::vector<const Type *> *binding,
                                std::vector<long long> *values, std::string *why, Packs *packs) {
    // [temp.deduct.call]/1 and CWG 1591: a braced list - the array deductionArgType makes of it - gives
    // initializer_list<P'> its P' from the elements and `T (&&)[N]` its T and N; anything else it leaves
    // as it found it, a non-deduced context.
    if (arg != nullptr && arg->isArray() && !arg->isReference()) {
        const Type *p = (param->isReference() ? param->referent() : param)->unqualified();
        if (p->isSpecialization() && p->templateName() == "initializer_list" && !p->templateArgs().empty() &&
            p->templateArgs()[0].type != nullptr)
            return deduceOne(p->templateArgs()[0].type, arg->pointee(), binding, values, why, packs);
        if (param->isReference() && p->isArray()) return deduceOne(p, arg, binding, values, why, packs, true);
        return true;
    }
    const std::vector<const Type *> before = *binding;
    const std::vector<long long> beforeValues = values ? *values : std::vector<long long>();
    const Packs beforePacks = packs ? *packs : Packs();
    if (deduceOne(param, arg, binding, values, why, packs)) return true;
    if (packs) *packs = beforePacks;
    // An lvalue argument comes as a reference to its type; a parameter that is no reference takes the type
    // ([temp.deduct.call]/2). 1.81: `f(p, p)` for `const E<T> *` and a `D *` variable missed E<D> - only `&x`,
    // a prvalue, reached the base - and the `(...)` overload was chosen without a word.
    if (!param->isReference() && arg != nullptr && arg->isReference()) arg = arg->referent();
    const bool viaPointer = param->isPointer() && arg != nullptr && arg->isPointer();
    const Type *p = viaPointer ? param->pointee() : param->isReference() ? param->referent() : param;
    const Type *a = viaPointer ? arg->pointee() : arg != nullptr && arg->isReference() ? arg->referent() : arg;
    if (p == nullptr || a == nullptr) return false;
    p = p->unqualified();
    if (!p->isSpecialization() || !a->unqualified()->isStructOrUnion()) return false;
    std::vector<const Type *> bases;
    basesNamed(a, p->templateName(), &bases);
    int found = 0;
    std::vector<const Type *> got;
    std::vector<long long> gotValues;
    Packs gotPacks;
    for (const Type *b : bases) {
        std::vector<const Type *> tryBinding = before;
        std::vector<long long> tryValues = beforeValues;
        Packs tryPacks = beforePacks;
        std::string ignored;
        const Type *as = viaPointer ? types_.pointerTo(b) : b;
        if (!deduceOne(p, viaPointer ? b : as, &tryBinding, values ? &tryValues : nullptr, &ignored,
                       packs ? &tryPacks : nullptr))
            continue;
        if (++found == 1) { got = tryBinding; gotValues = tryValues; gotPacks = tryPacks; }
    }
    if (found != 1) return false;
    *binding = got;
    if (values) *values = gotValues;
    if (packs) *packs = gotPacks;
    return true;
}

// **Tag dispatch**: a class parameter by value naming no template parameter
// and an argument that cannot become it: the candidate is out before its body
// is read, which would otherwise be made for a call that never reaches it.
bool Parser::tagRulesOut(const Type *param, const Type *given) {
    if (given == nullptr || param->isReference()) return false;
    const Type *to = param->unqualified();
    const Type *from = (given->isReference() ? given->referent() : given)->unqualified();
    if (!to->isStructOrUnion() || !from->isStructOrUnion() || to == from) return false;
    if (!to->isComplete() || !from->isComplete()) return false;
    ExprPtr probe(Var::local("$probe", 0));
    probe->setType(from);
    return rankArgument(*probe, to) == Rank::None;
}

bool Parser::deduceTemplateArguments(const TemplateDecl &decl,
                                     const std::vector<const Type *> &argTypes,
                                     std::vector<const Type *> *binding,
                                     std::vector<long long> *values,
                                     Packs *packs,
                                     std::string *why,
                                     const std::vector<const Type *> *givenTypes,
                                     const std::vector<long long> *givenValues) {
    packs->assign(decl.params.size(), std::vector<const Type *>());
    values->assign(decl.params.size(), 0);

    // [temp.arg.explicit]/3: the arguments written stand in the pattern as themselves; the rest are deduced.
    const std::size_t given = givenTypes != nullptr ? givenTypes->size() : 0;
    std::vector<const Type *> pattern(decl.params.size());
    std::vector<long long> seed(decl.params.size(), 0);
    for (std::size_t i = 0; i < decl.params.size(); i++) {
        pattern[i] = i < given ? (*givenTypes)[i] : types_.templateParam(static_cast<int>(i));
        if (i < given && givenValues != nullptr) seed[i] = (*givenValues)[i];
    }
    std::string ignored;
    const Type *fn = readTemplateDeclaration(decl, pattern, seed, &ignored);

    // **A trailing parameter pack takes every argument the others leave**,
    // may be empty (so `<`, not `!=`), and is the last template parameter
    // but for any with a default after it, [temp.param]/11.
    std::size_t packAt = decl.params.size();
    for (std::size_t k = decl.params.size(); k-- > 0; )
        if (decl.params[k].isPack) { packAt = k; break; }
    const bool hasPack = packAt < decl.params.size() &&
                         !fn->params().empty() && fn->params().back()->kind() == Kind::PackExpansion;
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
    for (std::size_t i = 0; i < deduceFrom; i++) {
        // **A parameter naming none of the template's own parameters deduces nothing** - [temp.deduct.call]/4
        // and [temp.arg.explicit]/6: `emplace(const_iterator, A &&...)` given an iterator converts it in
        // overload resolution rather than failing here for not being a `const_iterator`.
        bool named = false;
        for (std::size_t k = 0; k < decl.params.size() && !named; k++) named = mentionsParam(fn->params()[i], k);
        if (!named && tagRulesOut(fn->params()[i], argTypes[i])) {
            *why = "argument " + std::to_string(i + 1) + " cannot become '" + fn->params()[i]->describe() + "'";
            return false;
        }
        if (!named) continue;
        if (!deduceFromArgument(fn->params()[i], argTypes[i], binding, values, why, packs)) {
            *why = "'" + decl.params[i < decl.params.size() ? i : 0].name +
                   "' cannot be worked out from this call: " + *why;
            return false;
        }
    }
    if (hasPack) {
        std::vector<const Type *> members;
        // Each member deduced as one parameter of the expansion's shape would be: `A &&...` forwards.
        const Type *shape = fn->params().back()->kind() == Kind::PackExpansion ? fn->params().back()->pointee() : nullptr;
        for (std::size_t i = fixed; i < argTypes.size(); i++) {
            const Type *a = argTypes[i];
            if (shape != nullptr && forwardingReference(shape) && a->isReference()) members.push_back(a);
            else if (shape != nullptr && shape->isReference())
                members.push_back(shape->referent()->unqualified() != shape->referent()
                                      ? (a->isReference() ? a->referent() : a)->unqualified()
                                      : (a->isReference() ? a->referent() : a));
            else members.push_back(decayedType(a->isReference() ? a->referent() : a));
        }
        (*packs)[packAt] = members;
    }
    for (std::size_t i = 0; i < given; i++) {
        (*binding)[i] = pattern[i];
        (*values)[i] = seed[i];
    }
    for (std::size_t i = 0; i < binding->size(); i++) {
        // A pack deduced from an expansion was marked in `binding` by its own parameter; the function
        // parameter pack binds through `packs` alone. Neither is a type to leave in the binding.
        if (decl.params[i].isPack) { (*binding)[i] = nullptr; continue; }
        if ((*binding)[i] != nullptr) continue;
        // [temp.deduct]/7: a default that does not substitute makes the deduction fail, and no more.
        bool took = false;
        try {
            Trial trial(this);
            // A friend template's default may name its class template's parameters: `template <int Q = P>`.
            Bound classBound(this);
            if (!decl.friendOf.empty() && !decl.classParams.empty())
                bindTemplateParameters(decl.classParams, decl.classBinding, decl.classValues, decl.classPacks,
                                       &classBound.undo);
            took = defaultTemplateArgument(decl, i, binding, values, packs);
        } catch (const SubstitutionFailure &f) {
            if (f.unsupported) src_.fail(f.pos, f.why);
            *why = "the default argument of '" + decl.params[i].name + "' does not substitute: " + f.why;
            return false;
        }
        if (!took) {
            *why = "'" + decl.params[i].name + "' appears in no parameter, so "
                   "there is nothing in the call to work it out from - write "
                   "the arguments out";
            return false;
        }
    }
    for (std::size_t i = 0; i < decl.params.size() && i < binding->size(); i++) {
        const TemplateParam &p = decl.params[i];
        if (p.type == nullptr || p.isPack || p.typeAt == 0 || !dependentType(p.type)) continue;
        try {
            Trial trial(this);
            resolveParamType(decl.params, i, *binding, *values);
        } catch (const SubstitutionFailure &f) {
            if (f.unsupported) src_.fail(f.pos, f.why);
            *why = "the type of '" + p.name + "' does not substitute: " + f.why;
            return false;
        }
    }
    return true;
}

const Type *Parser::resolveParamType(const std::vector<TemplateParam> &params, std::size_t i,
                                     const std::vector<const Type *> &binding, const std::vector<long long> &values) {
    const TemplateParam &p = params[i];
    std::vector<TemplateParam> earlier(params.begin(), params.begin() + static_cast<long>(i));
    std::vector<const Type *> eb(binding.begin(), binding.begin() + static_cast<long>(std::min(i, binding.size())));
    eb.resize(i, nullptr);
    std::vector<long long> ev(values.begin(), values.begin() + static_cast<long>(std::min(i, values.size())));
    ev.resize(i, 0);
    std::vector<Shadow> undo;
    struct Back {
        Parser *p; std::vector<Shadow> *u; std::size_t at; bool pattern; std::vector<std::string> ns;
        ~Back() { p->unbindTemplateParameters(*u); p->at_ = at; p->patternOnly_ = pattern; p->namespaceStack_ = ns; }
    } back{ this, &undo, at_, patternOnly_, namespaceStack_ };
    bindTemplateParameters(earlier, eb, ev, Packs(i), &undo);
    patternOnly_ = false;
    namespaceStack_ = p.typeNamespace;
    at_ = p.typeAt;
    StorageClass sc;
    Qualifiers quals;
    const Type *t = declarator(specifiers(&sc, &quals), true).type;
    resolvedParamTypes_[std::make_pair(p.type, binding)] = t;
    return t;
}

// Bit 1 const, bit 2 `&`, bit 4 `&&`; a pack is a known pack name, so nothing else is read as one.
std::size_t Parser::packDeclarator(int *wrap, std::size_t *nameAt) const {
    std::size_t k = 0;
    *wrap = 0;
    if (peekAt(k).is("const")) { *wrap |= 1; k++; }
    if (peekAt(k).kind != TokenKind::Ident || packs_.count(peekAt(k).text) == 0) return 0;
    *nameAt = k++;
    if (peekAt(k).is("&")) { *wrap |= 2; k++; }
    else if (peekAt(k).is("&&")) { *wrap |= 4; k++; }
    if (!peekAt(k).is("...")) return 0;
    return k + 1;
}

// One member as its parameter is written - `A &&` with A bound to `int &` collapses to `int &`.
const Type *Parser::wrapPackMember(const Type *m, int wrap) {
    if ((wrap & 1) && !m->isReference()) m = types_.withConst(m);
    if (wrap & 2) return types_.referenceTo(m->isReference() ? m->referent() : m);
    if (wrap & 4) return m->isReference() ? m : types_.rvalueReferenceTo(m);
    return m;
}

// [temp.variadic]/5: a pattern followed by `...` is one argument per member. Where the pattern ends -
// past its `...`, which the next `,`, `)`, `]`, `}` or (with `angle`) `>` at depth 0 must follow - and
// how many members the packs it names have, which must agree; 0 where the tokens are not that.
std::size_t Parser::packPatternEnd(bool angle, std::size_t *members) const {
    std::size_t end = at_;
    // Brackets and template argument lists counted apart: `id(sum(c.fetch<D>()...))` - the member template's
    // `<` was not counted and its `>` closed `sum(`, so the pattern ended inside it (1.82: "expected ','").
    int depth = 0, angles = 0;
    for (; end < tokens_.size() && tokens_[end].kind != TokenKind::End; end++) {
        const Token &t = tokens_[end];
        if (t.is("(") || t.is("[") || t.is("{")) depth++;
        else if (t.is(")") || t.is("]") || t.is("}")) { if (depth == 0) break; depth--; }
        else if (t.is("<") && end > at_ &&
                 (isTemplateName(tokens_[end - 1].text) || tokens_[end - 1].text.find("_cast") != std::string::npos ||
                  (packs_.count(tokens_[end - 1].text) != 0 && packs_.at(tokens_[end - 1].text).isTemplate) ||
                  // a member template named after `.`, `->` or `template`
                  (tokens_[end - 1].kind == TokenKind::Ident && end > at_ + 1 &&
                   (tokens_[end - 2].is(".") || tokens_[end - 2].is("->") || tokens_[end - 2].is("template")))))
            angles++;
        else if ((t.is(">") || t.is(">>")) && angles > 0) angles = std::max(0, angles - (t.is(">>") ? 2 : 1));
        else if ((t.is(">") || t.is(">>")) && angle && depth == 0) break;
        else if (t.is(",") && depth == 0 && angles == 0) break;
    }
    if (end < at_ + 2 || !tokens_[end - 1].is("...")) return 0;
    *members = 0;
    bool any = false;
    for (std::size_t i = at_; i + 1 < end; i++) {
        auto pk = tokens_[i].kind == TokenKind::Ident ? packs_.find(tokens_[i].text) : packs_.end();
        if (pk == packs_.end()) continue;
        if (any && pk->second.types.size() != *members)
            src_.fail(tokens_[end - 1].pos, "the packs expanded by this '...' have different lengths");
        *members = pk->second.types.size();
        any = true;
    }
    return any ? end : 0;
}

// The pattern's tokens [from, end - 1) copied to the end of the stream - as a lambda's body is - with
// the function parameter pack renamed to `a$m` and every other pack bound to its m-th member, a type
// as a type name and a value as an enumerator; at_ is left at the copy, which ends in an End token.
void Parser::bindPatternMember(std::size_t from, std::size_t end, std::size_t m, std::vector<Shadow> *undo) {
    std::vector<TemplateParam> bound;
    std::vector<const Type *> types;
    std::vector<long long> values;
    const std::size_t copy = tokens_.size();
    for (std::size_t i = from; i + 1 < end; i++) {
        Token t = tokens_[i];
        auto pk = t.kind == TokenKind::Ident ? packs_.find(t.text) : packs_.end();
        if (pk != packs_.end()) {
            const PackBinding &pb = pk->second;
            // **In a pattern the pack stands for itself** and has no m-th member to bind: a pattern over
            // it in a signature is what Itanium spells `XspmlT_Li2EE`, which is not written here.
            if (pb.standsForItself())
                src_.fail(t.pos, "a pattern over the parameter pack '" + t.text + "' in a template's "
                                 "signature is not supported yet - '" + t.text + "...' alone is");
            if (m < pb.names.size() && pb.names[m] == t.text + "$" + std::to_string(m)) {
                t.text = pb.names[m];
            } else {
                TemplateParam p;
                p.name = t.text;
                if (pb.isValue) p.type = pb.valueType;
                else p.isTemplate = pb.types[m]->isTemplateName();
                bound.push_back(p);
                types.push_back(pb.isValue ? pb.valueType : pb.types[m]);
                values.push_back(pb.isValue ? pb.values[m] : 0);
            }
        }
        tokens_.push_back(t);
    }
    Token stop;
    stop.kind = TokenKind::End;
    tokens_.push_back(stop);
    bindTemplateParameters(bound, types, values, Packs(bound.size()), undo);
    at_ = copy;
}

// `f(g(a)...)`, `std::forward<A>(a)...`, `f(Ns...)`, `f((Ns * 2)...)`: one argument per member.
bool Parser::expandPackPattern(std::vector<ExprPtr> &args) {
    std::size_t members = 0;
    const std::size_t end = packPatternEnd(false, &members);
    if (end == 0) return false;
    const std::size_t from = at_;
    for (std::size_t m = 0; m < members; m++) {
        std::vector<Shadow> undo;
        struct Unbind {
            Parser *p; std::vector<Shadow> *u;
            ~Unbind() { p->unbindTemplateParameters(*u); }
        } unbind{ this, &undo };
        bindPatternMember(from, end, m, &undo);
        args.push_back(assign());
        if (peek().kind != TokenKind::End)
            src_.fail(peek().pos, "this pack expansion's pattern is not one expression");
    }
    at_ = end;
    return true;
}

// `{ 0, Ns... }`, `{ (Ns * 2)... }`: one element per member, each read as an initialiser.
bool Parser::expandPackInitialiser(std::vector<Init> &items) {
    std::size_t members = 0;
    const std::size_t end = packPatternEnd(false, &members);
    if (end == 0) return false;
    const std::size_t from = at_;
    for (std::size_t m = 0; m < members; m++) {
        std::vector<Shadow> undo;
        struct Unbind {
            Parser *p; std::vector<Shadow> *u;
            ~Unbind() { p->unbindTemplateParameters(*u); }
        } unbind{ this, &undo };
        bindPatternMember(from, end, m, &undo);
        items.push_back(parseInitialiser());
        if (peek().kind != TokenKind::End)
            src_.fail(peek().pos, "this pack expansion's pattern is not one initialiser");
    }
    at_ = end;
    return true;
}

// `List<(Ns * 2)...>`: one value per member, each a constant expression of the element type.
bool Parser::expandPackValues(TemplateArg &a) {
    std::size_t members = 0;
    const std::size_t end = packPatternEnd(true, &members);
    if (end == 0) return false;
    const std::size_t from = at_;
    for (std::size_t m = 0; m < members; m++) {
        std::vector<Shadow> undo;
        struct Unbind {
            Parser *p; std::vector<Shadow> *u;
            ~Unbind() { p->unbindTemplateParameters(*u); }
        } unbind{ this, &undo };
        bindPatternMember(from, end, m, &undo);
        const std::size_t argPos = peek().pos;
        const long long v = constantExpression("a template argument");
        if (peek().kind != TokenKind::End)
            src_.fail(peek().pos, "this pack expansion's pattern is not one expression");
        checkNonTypeArgument(a.type, v, argPos);
        a.pack.push_back(a.type);
        a.packValues.push_back(v);
    }
    at_ = end;
    return true;
}

// [temp.arg.nontype]/1: a converted constant expression, which may not narrow. An 8-byte type is
// left alone: the value's own signedness is not kept here.
void Parser::checkNonTypeArgument(const Type *pt, long long v, std::size_t argPos) const {
    if (!(pt->isInteger() && pt->tag().empty() && pt->size(target_) < 8)) return;
    const int bits = pt->kind() == Kind::Bool ? 1 : 8 * pt->size(target_);
    const bool sgn = pt->kind() != Kind::Bool && pt->isSigned(target_);
    const long long lo = sgn ? -(1LL << (bits - 1)) : 0;
    const long long hi = sgn ? (1LL << (bits - 1)) - 1 : (1LL << bits) - 1;
    if (v < lo || v > hi)
        src_.fail(argPos, "the template argument is " + std::to_string(v) +
                          ", which cannot be narrowed to '" + pt->describe() + "'");
}

// **[temp.arg.nontype]/1: an argument for a pointer, reference, member pointer
// or nullptr_t parameter** (`&gv`, `gv`, `&S::m`, a null of its type), read by
// name; the answer is its index in addressArgs_. /5 allows cv and decay.
long long Parser::addressArgument(const Type *pt, std::size_t pos) {
    pt = pt->unqualified();
    std::size_t k = 0, open = 0;
    while (peekAt(k).is("(")) { k++; open++; }
    const bool amp = peekAt(k).is("&");
    if (amp) k++;
    while (peekAt(k).is("(")) { k++; open++; }
    std::string written;
    if (peekAt(k).kind == TokenKind::Ident) {
        written = peekAt(k).text;
        k++;
        while (peekAt(k).is("::") && peekAt(k + 1).kind == TokenKind::Ident) {
            written += "::" + peekAt(k + 1).text;
            k += 2;
        }
    }
    for (; open > 0 && peekAt(k).is(")"); open--) k++;
    const bool named = !written.empty() && open == 0 &&
                       (peekAt(k).is(",") || peekAt(k).is(">") || peekAt(k).is(">>"));
    AddressArg a;
    auto intern = [&]() {
        for (std::size_t i = 0; i < addressArgs_.size(); i++)
            if (addressArgs_[i].kind == a.kind && addressArgs_[i].spelling == a.spelling &&
                addressArgs_[i].entity == a.entity)
                return static_cast<long long>(i);
        addressArgs_.push_back(a);
        return static_cast<long long>(addressArgs_.size() - 1);
    };
    if (!named) {
        ExprPtr e = conditional();
        const Type *et = e->type() != nullptr ? e->type()->unqualified() : nullptr;
        long long v = 1;
        const Expr *under = e.get();
        while (const Cast *c = dynamic_cast<const Cast *>(under)) under = &c->value();
        if (const Num *n = dynamic_cast<const Num *>(under)) v = n->value();
        const bool folded = et != nullptr && !et->isMemberFunctionPointer() &&
                            (v == 0 || fold(*e, &v, pos));
        if (et != nullptr && et->isInteger() && folded && v == 0 && !pt->isReference())
            src_.fail(pos, "a null template argument for '" + pt->describe() + "' has to have that type - "
                           "[temp.arg.nontype]/5 converts no integer to a pointer; write 'nullptr'");
        const bool null = et != nullptr && (et->isNullPtr() || (et == pt && folded && v == 0));
        if (!null || pt->isReference())
            src_.fail(pos, "a template argument for '" + pt->describe() + "' has to name an object or a "
                           "function with linkage - '&name', or the name of an array or a function" +
                           std::string(pt->isReference() ? "; for a reference the object's name"
                                                         : ", or a null pointer") +
                           " ([temp.arg.nontype]/1) - and this is not one");
        a.kind = AddressArg::Null;
        a.spelling = "nullptr";
        return intern();
    }
    at_ += k;
    // A parameter of the instantiation being read, passed on - `call<F>()` in a template of F - is its binding.
    if (!amp && boundValueParams_.count(written) != 0)
        if (const EnumConst *e = findEnum(written))
            if (e->paramType != nullptr && takesAddress(e->paramType)) return e->value;
    if (findLocal(written) != nullptr)
        src_.fail(pos, "'" + written + "' is a local, and a template argument has to name an object or a "
                       "function with linkage ([temp.arg.nontype]/1) - a local has none");
    auto mismatch = [&]() {
        src_.fail(pos, "'" + std::string(amp ? "&" : "") + written + "' cannot be the argument for a "
                       "template parameter of type '" + pt->describe() + "' - [temp.arg.nontype]/5 allows " +
                       (pt->isReference() ? "no conversion for a reference, which names the object itself"
                                          : "a qualification or an array-to-pointer conversion and nothing else"));
    };
    auto entityOf = [&](bool function) {
        if (target_.microsoftNames()) {
            if (!a.symbol.empty() && a.symbol[0] == '?') return a.symbol;
            // cl spells a C name in a template argument as if it had C++ linkage: `$1?cv@@3HA`.
            std::string out, why;
            const bool ok = function ? microsoftFunctionName(a.key, a.type, false, &out, &why)
                                     : microsoftDataName(a.key, a.type, &out, &why);
            if (!ok) src_.fail(pos, "'" + a.key + "' cannot be named in a template argument: " + why);
            return out;
        }
        // clang mangles a name the ABI leaves alone as if it were unqualified: `L_Z2gvE`.
        if (a.symbol.compare(0, 2, "_Z") == 0) return a.symbol;
        return "_Z" + std::to_string(a.key.size()) + a.key;
    };
    auto object = [&]() {
        const Type *ot = a.type;
        auto qualifies = [](const Type *from, const Type *to) {
            return from->unqualified() == to->unqualified() && (!from->isConst() || to->isConst());
        };
        bool ok;
        if (pt->isReference()) {
            ok = !amp && qualifies(ot, pt->referent());
            a.spelling = a.key;
        } else if (pt->isPointer() && amp) {
            ok = qualifies(ot, pt->pointee());
            a.spelling = "&" + a.key;
        } else {
            ok = pt->isPointer() && ot->isArray() && qualifies(ot->pointee(), pt->pointee());
            a.spelling = a.key;
        }
        if (!ok) mismatch();
        a.kind = AddressArg::Object;
        a.entity = entityOf(false);
        return intern();
    };
    auto function = [&](const Signature &f, const std::string &key) {
        const Type *fn = types_.functionType(f.returns, f.params, f.variadic);
        if (pt->isReference() ? amp || pt->referent()->unqualified() != fn : !pt->isPointer() || pt->pointee() != fn)
            mismatch();
        a.kind = AddressArg::Function;
        a.key = key;
        a.symbol = f.symbol;
        a.type = fn;
        a.spelling = (pt->isReference() ? "" : "&") + key;
        a.entity = entityOf(true);
        return intern();
    };
    const std::size_t cut = written.rfind("::");
    if (cut != std::string::npos) {
        const std::string member = written.substr(cut + 2);
        const Type *cls = findTypedef(written.substr(0, cut));
        if (cls != nullptr && cls->isStructOrUnion()) {
            cls = cls->unqualified();
            const std::string key = cls->tag() + "::" + member;
            if (const Member *m = cls->findMember(member)) {
                checkAccessible(cls, *m, pos);
                if (!amp || types_.memberPointerTo(cls, m->type) != pt) mismatch();
                a.kind = AddressArg::DataMember;
                a.cls = cls;
                a.type = m->type;
                a.offset = m->offset;
                a.key = key;
                a.spelling = "&" + key;
                if (!target_.microsoftNames()) a.entity = itaniumStaticMemberName(cls->tag(), cls, member);
                return intern();
            }
            if (const std::vector<std::size_t> *set = overloadsOf(key)) {
                const Signature *f = set->size() > 1 ? pickByTarget(*set, cls, pt) : &functions_[(*set)[0]];
                if (f != nullptr && f->isStaticMember) {
                    a.cls = cls;
                    return function(*f, key);
                }
                if (f == nullptr || !amp || !pt->isMemberFunctionPointer() || pt->enclosing() != cls ||
                    types_.functionType(f->returns, f->params, f->variadic, f->refThis,
                                        thisCvOf(*f)) != pt->pointee())
                    mismatch();
                a.kind = AddressArg::MemberFunction;
                a.cls = cls;
                a.function = static_cast<std::size_t>(f - &functions_[0]);
                a.key = key;
                a.symbol = f->symbol;
                a.entity = f->symbol;
                a.spelling = "&" + key;
                // A virtual one is cl's vcall thunk, what `&S::f` holds; Itanium names the function.
                if (f->isVirtual && target_.microsoftNames()) {
                    int index = -1;
                    const std::vector<VSlot> &slots = vtables_[cls->tag()];
                    for (std::size_t i = 0; i < slots.size(); i++)
                        if (overrides(slots[i], f->name, f->params, f->constThis, f->refThis, f->volatileThis))
                            index = static_cast<int>(i);
                    std::string why;
                    if (index < 0 || !microsoftVcallThunkName(cls, index * pointerBytes(), &a.entity, &why))
                        src_.fail(pos, "'&" + key + "' cannot be named in a template argument: " +
                                       (index < 0 ? std::string("it has no slot in '") + cls->describe() +
                                                        "'s own vtable" : why));
                }
                return intern();
            }
            if (const Type::StaticMember *s = cls->findStaticMember(member)) {
                if (s->access != Access::Public && !insideAccessOf(cls, s->access) && !isFriendOf(cls))
                    src_.fail(pos, "'" + key + "' is " + (s->access == Access::Private ? "private" : "protected"));
                if (s->isThreadLocal)
                    src_.fail(pos, "'" + key + "' is thread_local, and a template argument has to name an "
                                   "object with static storage duration ([temp.arg.nontype]/1)");
                a.key = key;
                a.cls = cls;
                a.symbol = s->symbol;
                a.type = s->type;
                a.staticMember = true;
                return object();
            }
        }
    }
    if (const GlobalSym *g = findGlobal(written)) {
        if (g->isThreadLocal)
            src_.fail(pos, "'" + written + "' is thread_local, and a template argument has to name an object "
                           "with static storage duration ([temp.arg.nontype]/1)");
        if (g->type->isReference())
            src_.fail(pos, "'" + written + "' is a reference, and a template argument has to name an object "
                           "or a function ([temp.arg.nontype]/1) - a reference is neither");
        a.key = g->name;
        a.symbol = g->symbol;
        a.type = g->type;
        a.readOnly = g->isConst;
        return object();
    }
    const std::string key = qualifyForLookup(written, &Parser::hasFunctionNamed);
    if (const std::vector<std::size_t> *set = overloadsOf(key)) {
        const Signature *f = set->size() > 1 ? pickByTarget(*set, nullptr, pt) : &functions_[(*set)[0]];
        if (f == nullptr) mismatch();
        return function(*f, key);
    }
    src_.fail(pos, "'" + written + "' was not declared");
}

// The manglers' half of an address argument: TemplateArg::form and what goes with it.
void Parser::describeAddress(TemplateArg &arg) const {
    if (arg.isType || arg.isParam || arg.expr != nullptr || arg.type == nullptr || !takesAddress(arg.type)) return;
    if (arg.value < 0 || static_cast<std::size_t>(arg.value) >= addressArgs_.size()) return;
    const AddressArg &a = addressArgs_[static_cast<std::size_t>(arg.value)];
    const Type *pt = arg.type->unqualified();
    arg.address = a.spelling;
    arg.entity = a.entity;
    arg.name = a.key;
    arg.scope = a.cls;
    if (a.kind == AddressArg::Function) arg.function = a.type;
    if (a.kind == AddressArg::MemberFunction) {
        const Signature &f = functions_[a.function];
        arg.function = types_.functionType(f.returns, f.params, f.variadic, f.refThis,
                                           thisCvOf(f));
    }
    if (a.kind == AddressArg::Null) arg.form = 'n';
    else if (a.kind == AddressArg::DataMember) { arg.form = target_.microsoftNames() ? 'm' : 'p'; arg.offset = a.offset; }
    else if (pt->isReference()) arg.form = 'r';
    else if (a.kind == AddressArg::Object && a.type->isArray() && !pt->pointee()->isArray()) {
        arg.form = 'a';
        arg.element = pt->pointee();
    } else if (a.kind == AddressArg::Object && pt->pointee() != a.type) arg.form = 'c';
    else arg.form = 'p';
}

// **A bound address argument as the expression its parameter names**, built afresh in the function reading
// it: [temp.param]/6 makes a pointer's a prvalue and a reference's the object or function itself.
ExprPtr Parser::addressExpression(long long index, const Type *paramType, std::size_t pos) {
    const AddressArg a = addressArgs_[static_cast<std::size_t>(index)];
    const Type *pt = paramType->unqualified();
    if (a.kind == AddressArg::Null) {
        ExprPtr n(new Num(0LL));
        n->setType(types_.get(Kind::NullPtr));
        if (pt->isNullPtr()) return n;
        return convert(std::move(n), pt);
    }
    if (a.kind == AddressArg::DataMember) {
        ExprPtr n(new Num(static_cast<long long>(a.offset)));
        n->setType(pt);
        return n;
    }
    if (a.kind == AddressArg::MemberFunction) {
        const Signature f = functions_[a.function];
        return memberFunctionAddress(a.cls, f, a.key, pos);
    }
    Var *v = Var::global(a.key);
    v->setSymbol(a.symbol);
    v->setReadOnly(a.readOnly);
    ExprPtr o(v);
    o->setType(a.type);
    if (a.staticMember && unevaluated_ == 0) usedStaticMembers_.insert(a.symbol);
    if (pt->isReference()) {
        if (a.kind == AddressArg::Object) o->setType(pt->referent());
        return o;
    }
    if (a.kind == AddressArg::Object && a.type->isArray() && !pt->pointee()->isArray())
        return convert(decay(std::move(o)), pt);
    const Type *of = o->type();
    ExprPtr n(new Unary('&', std::move(o)));
    n->setType(types_.pointerTo(of));
    if (of == pt->pointee()) return n;
    return convert(std::move(n), pt);
}

// [temp.param]/9 on the deduced path: a parameter the call could not work out takes its
// default, replayed with every parameter before it bound - `class U = T` reads T's binding.
bool Parser::defaultTemplateArgument(const TemplateDecl &decl, std::size_t i,
                                     std::vector<const Type *> *binding,
                                     std::vector<long long> *values, const Packs *packs) {
    const TemplateParam &p = decl.params[i];
    if (p.defBegin == 0) return false;
    std::vector<Shadow> undo;
    struct Unbind {
        Parser *p; std::vector<Shadow> *u;
        ~Unbind() { p->unbindTemplateParameters(*u); }
    } unbind{ this, &undo };
    for (std::size_t k = 0; k < i; k++) {
        // A pack before it is bound to the members deduced for it - `class = enable_if<sizeof...(V) == 2>`.
        Packs onePack(1, std::vector<const Type *>());
        if (decl.params[k].isPack && packs != nullptr && k < packs->size()) onePack[0] = (*packs)[k];
        else if ((*binding)[k] == nullptr) return false;
        std::vector<TemplateParam> one(1, decl.params[k]);
        if (k < p.defNames.size()) one[0].name = p.defNames[k];
        std::vector<const Type *> ob(1, (*binding)[k]);
        std::vector<long long> ov(1, (*values)[k]);
        bindTemplateParameters(one, ob, ov, onePack, &undo);
    }
    const std::size_t saved = at_;
    struct InArgs {
        bool &f; bool was;
        std::size_t &split; std::size_t splitWas;
        ~InArgs() { f = was; split = splitWas; }
    } inArgs{ inTemplateArgs_, inTemplateArgs_, angleSplit_, angleSplit_ };
    inTemplateArgs_ = true;                    // the `>` after `N = 3` closes the list
    // A member template's default is read in its class and namespace, as its signature is:
    // `class = enable_if<is_constructible<value_type, P &&>::value>` names map's own typedef.
    struct Owner {
        Parser *p; std::string tag, name, signature; std::vector<std::string> ns;
        ~Owner() { p->inlineOwner_ = tag; p->inlineOwnerName_ = name; p->signatureOwner_ = signature; p->namespaceStack_ = ns; }
    } owner{ this, inlineOwner_, inlineOwnerName_, signatureOwner_, namespaceStack_ };
    if (decl.isMember && !decl.ownerTag.empty()) {
        inlineOwner_ = decl.ownerTag;
        inlineOwnerName_ = decl.ownerLocal;
        signatureOwner_ = decl.ownerTag;
    }
    if (!decl.ns.empty()) namespaceStack_ = namespacePathStack(decl.ns);
    at_ = p.defBegin;
    if (p.isTemplate) {
        const TemplateDecl &named = templateTemplateArgument(decl, p);
        (*binding)[i] = named.ttParam >= 0 ? types_.templateParam(named.ttParam) : templateNameTypeOf(named);
    } else if (p.type == nullptr) {
        StorageClass sc;
        Qualifiers quals;
        const Type *base = specifiers(&sc, &quals);
        (*binding)[i] = declarator(base, true).type;
    } else {
        (*binding)[i] = paramTypeIn(p, *binding);
        (*values)[i] = takesAddress((*binding)[i]) ? addressArgument((*binding)[i], peek().pos)
                                                   : constantExpression("a default template argument");
    }
    at_ = saved;
    return true;
}

// Whether a type holds a pattern's `R...` anywhere in a template-id's arguments.
bool Parser::mentionsPackExpansion(const Type *t) {
    if (t == nullptr) return false;
    if (t->unqualified() != t) return mentionsPackExpansion(t->unqualified());
    if (t->isPointer() || t->isArray()) return mentionsPackExpansion(t->pointee());
    if (t->isReference()) return mentionsPackExpansion(t->referent());
    if (!t->isSpecialization()) return false;
    for (std::size_t k = 0; k < t->templateArgs().size(); k++) {
        const TemplateArg &a = t->templateArgs()[k];
        if (a.expandsParam >= 0) return true;
        if (a.isPack) {
            for (std::size_t m = 0; m < a.pack.size(); m++)
                if (mentionsPackExpansion(a.pack[m])) return true;
        } else if (a.isType && mentionsPackExpansion(a.type)) {
            return true;
        }
    }
    return false;
}

// Whether parameter `i` appears anywhere in a pattern. One a specialization never
// mentions could not be worked out from any argument list, so it could never be
// chosen - worth refusing where it is written rather than leaving it silent.
static bool mentionsParam(const Type *t, std::size_t i) {
    if (t == nullptr) return false;
    if (t->unqualified() != t) return mentionsParam(t->unqualified(), i);
    if (t->kind() == Kind::TemplateParam)
        return static_cast<std::size_t>(t->length()) == i;
    if (t->isArray() && t->boundParam() == static_cast<int>(i)) return true;
    if (t->kind() == Kind::PackExpansion) return mentionsParam(t->pointee(), i);
    if (t->isFunction()) {
        for (const Type *p : t->params())
            if (mentionsParam(p, i)) return true;
        return mentionsParam(t->returns(), i);
    }
    if (t->isMemberPointer() || t->isMemberFunctionPointer())
        return mentionsParam(t->pointee(), i) || mentionsParam(t->enclosing(), i);
    if (t->isPointer() || t->isArray()) return mentionsParam(t->pointee(), i);
    if (t->isReference()) return mentionsParam(t->referent(), i);
    if (t->isSpecialization()) {
        int tt;
        if (ttRefName(t->templateName(), &tt) && static_cast<std::size_t>(tt) == i) return true;
        for (std::size_t k = 0; k < t->templateArgs().size(); k++) {
            const TemplateArg &a = t->templateArgs()[k];
            if (a.isPack) {
                if (a.expandsParam == static_cast<int>(i)) return true;
                for (std::size_t m = 0; m < a.pack.size(); m++)
                    if (mentionsParam(a.pack[m], i)) return true;
            } else if (a.isType && mentionsParam(a.type, i)) {
                return true;
            } else if (a.isParam && static_cast<std::size_t>(a.paramIndex) == i) {
                return true;   // `is_placeholder<__cxx1_placeholder<N> >`: N a value of the nested template-id
            }
        }
        return false;
    }
    return false;
}

// **[temp.param]/11's second sentence**: a function template's pack may be followed by a
// parameter the call can work out - one a function parameter names - or one with a default.
void Parser::requireDeducibleAfterPack(const TemplateDecl &decl, const Type *fn) {
    const std::size_t from = afterPack(decl.params);
    if (from == static_cast<std::size_t>(-1)) return;
    for (std::size_t i = from; i < decl.params.size(); i++) {
        if (decl.params[i].defBegin != 0 || decl.params[i].isPack) continue;
        bool named = false;
        if (fn != nullptr && fn->kind() == Kind::Function)
            for (std::size_t k = 0; k < fn->params().size() && !named; k++)
                named = mentionsParam(fn->params()[k], i);
        if (!named)
            src_.fail(decl.params[i].pos, "'" + decl.params[i].name + "' follows the parameter pack '" +
                                          decl.params[from - 1].name + "' and no function parameter "
                                          "names it, so a call could never work it out - a parameter "
                                          "after a pack has to be deducible or defaulted");
    }
}

struct Parser::Trial::PacksCopy {
    std::map<std::string, PackBinding> m;
};

Parser::Trial::Trial(Parser *parser)
    : p(parser), at(parser->at_), classes(parser->classStack_.size()),
      pattern(parser->patternOnly_), split(parser->angleSplit_), inArgs(parser->inTemplateArgs_),
      packs(new PacksCopy{ parser->packs_ }) {
    p->src_.beginTrial();
}

Parser::Trial::~Trial() {
    p->src_.endTrial();
    p->at_ = at;
    p->classStack_.resize(classes);
    p->patternOnly_ = pattern;
    p->angleSplit_ = split;
    p->inTemplateArgs_ = inArgs;
    p->packs_.swap(packs->m);
    delete packs;
}

// [temp.deduct.type]. A pattern that is a pointer matches a pointer and
// nothing else - there is no conversion here for a mismatch to be forgiven
// by, which is what makes this stricter than deduction from a call.
bool Parser::matchPattern(const Type *pattern, const Type *arg,
                          std::vector<const Type *> *binding,
                          std::string *why) const {
    // **The qualifier first; both sides must agree**: `Box<const T>` matches
    // `Box<const int>`, not `Box<int>`; `Box<T>` both, T taking the qualifier;
    // `Is<volatile T>` given `const volatile int` makes T `const int`.
    if (pattern->unqualified() != pattern) {
        if ((pattern->isConst() && !arg->isConst()) || (pattern->isVolatile() && !arg->isVolatile())) {
            *why = "'" + arg->describe() + "' is not " +
                   (pattern->isConst() && !arg->isConst() ? "const" : "volatile");
            return false;
        }
        return matchPattern(pattern->unqualified(), withoutCvOf(arg, pattern),
                            binding, why);
    }

    // An expansion stands for any number of types, which one parameter is not - [temp.deduct.type]/9.
    if (arg->kind() == Kind::PackExpansion && pattern->kind() != Kind::PackExpansion) return false;

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
    // A pattern written without cv matches an argument with none: `K<T *>` does not take an `int *const`,
    // which only `K<const T>` and `K<T>` do. An array's cv is its element's, and matched there.
    if (arg->unqualified() != arg && !arg->isArray()) {
        *why = "'" + arg->describe() + "' is cv-qualified and '" + pattern->describe() + "' is not";
        return false;
    }

    if (pattern->isPointer())
        return arg->isPointer() &&
               matchPattern(pattern->pointee(), arg->pointee(), binding, why);
    // `T &` and `T &&` are two patterns: `remove_reference<T &&>` must not take an `int &`.
    if (pattern->isReference())
        return arg->isReference() && arg->isRValueReference() == pattern->isRValueReference() &&
               matchPattern(pattern->referent(), arg->referent(), binding, why);
    // `T[N]` binds N from the argument's bound - [temp.class.spec.match], as deduction does.
    if (pattern->isArray()) {
        if (!arg->isArray()) return false;
        const int bound = pattern->boundParam();
        if (bound >= 0 && matchValues_ != nullptr && arg->length() >= 0 &&
            static_cast<std::size_t>(bound) < matchValues_->size())
            (*matchValues_)[static_cast<std::size_t>(bound)] = arg->length();
        else if (pattern->length() != arg->length())
            return false;
        return matchPattern(pattern->pointee(), arg->pointee(), binding, why);
    }
    // `R(A)` and `R(As...)`: the return and each parameter, a trailing pack taking every parameter left.
    if (pattern->isFunction()) {
        if (!arg->isFunction() || pattern->isVariadicFn() != arg->isVariadicFn() ||
            pattern->refQualifier() != arg->refQualifier() || pattern->cvThis() != arg->cvThis())
            return false;
        const std::vector<const Type *> &pp = pattern->params(), &ap = arg->params();
        const bool tail = !pp.empty() && pp.back()->kind() == Kind::PackExpansion &&
                          pp.back()->pointee()->kind() == Kind::TemplateParam && matchPacks_ != nullptr;
        const std::size_t fixed = tail ? pp.size() - 1 : pp.size();
        if (tail ? ap.size() < fixed : ap.size() != fixed) return false;
        if (!matchPattern(pattern->returns(), arg->returns(), binding, why)) return false;
        for (std::size_t k = 0; k < fixed; k++)
            if (!matchPattern(pp[k], ap[k], binding, why)) return false;
        if (tail) {
            const std::size_t which = static_cast<std::size_t>(pp.back()->pointee()->length());
            if (which >= matchPacks_->size()) return false;
            (*matchPacks_)[which].assign(ap.begin() + static_cast<std::ptrdiff_t>(fixed), ap.end());
        }
        return true;
    }
    // `R (C::*)(P...) const` against a pointer to a member function: its function type, cv and ref-qualifier
    // included, and its class (item 43).
    if (pattern->isMemberFunctionPointer()) {
        if (!arg->isMemberFunctionPointer()) return false;
        const Type *pf = pattern->pointee(), *af = arg->pointee();
        if (pf->cvThis() != af->cvThis() || pf->refQualifier() != af->refQualifier()) return false;
        return matchPattern(pf, af, binding, why) && matchPattern(pattern->enclosing(), arg->enclosing(), binding, why);
    }
    // `T C::*` takes a pointer to a member function too, T its function type - is_member_pointer's pattern.
    if (pattern->isMemberPointer())
        return (arg->isMemberPointer() || arg->isMemberFunctionPointer()) &&
               matchPattern(pattern->pointee(), arg->pointee(), binding, why) &&
               matchPattern(pattern->enclosing(), arg->enclosing(), binding, why);

    if (pattern->isSpecialization()) {
        // `S<C<int>>` against `S<Box<int>>`: the template itself is what C is bound to.
        int tt;
        const bool viaParam = ttRefName(pattern->templateName(), &tt);
        // [temp.arg.template]/3: `A<U, Args...>` for a `template <class, class...> class A` against `One<int>` -
        // the parameter's pack takes whatever the fixed arguments leave, none included. Compared as flat lists of
        // types; the pattern's trailing pack holds its fixed members and, last, the expansion.
        if (viaParam && arg->isSpecialization() && !pattern->templateArgs().empty() &&
            pattern->templateArgs().back().isPack && !pattern->templateArgs().back().packIsValue &&
            pattern->templateArgs().back().expandsParam >= 0 && !pattern->templateArgs().back().pack.empty()) {
            std::vector<const Type *> fixedTypes;
            for (std::size_t k = 0; k + 1 < pattern->templateArgs().size(); k++) {
                const TemplateArg &p = pattern->templateArgs()[k];
                if (!p.isType || p.isTemplate || p.isPack) return false;
                fixedTypes.push_back(p.type);
            }
            const TemplateArg &tail = pattern->templateArgs().back();
            fixedTypes.insert(fixedTypes.end(), tail.pack.begin(), tail.pack.end() - 1);
            std::vector<const Type *> given;
            for (const TemplateArg &a : arg->templateArgs()) {
                if (a.isPack && !a.packIsValue) given.insert(given.end(), a.pack.begin(), a.pack.end());
                else if (a.isType && !a.isTemplate && !a.isPack) given.push_back(a.type);
                else return false;
            }
            if (given.size() < fixedTypes.size()) return false;
            const Type *deduced = templateOf(arg);
            const std::size_t i = static_cast<std::size_t>(tt);
            if ((*binding)[i] == nullptr) (*binding)[i] = deduced;
            else if ((*binding)[i] != deduced) return false;
            for (std::size_t k = 0; k < fixedTypes.size(); k++)
                if (!matchPattern(fixedTypes[k], given[k], binding, why)) return false;
            const std::size_t which = static_cast<std::size_t>(tail.expandsParam);
            if (matchPacks_ != nullptr && which < matchPacks_->size())
                (*matchPacks_)[which] = PackMembers(std::vector<const Type *>(given.begin() + static_cast<long>(fixedTypes.size()),
                                                                              given.end()));
            return true;
        }
        if (!arg->isSpecialization() ||
            (!viaParam && arg->templateName() != pattern->templateName()) ||
            arg->templateArgs().size() != pattern->templateArgs().size())
            return false;
        if (viaParam) {
            const Type *deduced = templateOf(arg);
            const std::size_t i = static_cast<std::size_t>(tt);
            if ((*binding)[i] == nullptr) (*binding)[i] = deduced;
            else if ((*binding)[i] != deduced) return false;
        }
        for (std::size_t i = 0; i < pattern->templateArgs().size(); i++) {
            const TemplateArg &p = pattern->templateArgs()[i];
            const TemplateArg &a = arg->templateArgs()[i];
            // `Ph<N>` against `Ph<3>`: N is matched to 3, as `T[N]` matches a bound.
            if (p.isParam && !a.isType && !a.isParam) {
                const std::size_t j = static_cast<std::size_t>(p.paramIndex);
                if (j < binding->size()) {
                    if ((*binding)[j] != nullptr && matchValues_ != nullptr && j < matchValues_->size() &&
                        (*matchValues_)[j] != a.value)
                        return false;
                    (*binding)[j] = a.type != nullptr ? a.type : types_.intType();
                    if (matchValues_ != nullptr && j < matchValues_->size()) (*matchValues_)[j] = a.value;
                }
                continue;
            }
            if (p.isType != a.isType || p.isTemplate != a.isTemplate || p.isPack != a.isPack) return false;
            if (p.isPack) {
                if (p.packIsValue != a.packIsValue) return false;
                // `Tup<H, T...>`: its fixed members match the argument's first ones, and its expansion the rest.
                if (p.expandsParam >= 0) {
                    const std::size_t fixed = p.pack.size() - 1;
                    if (a.pack.size() < fixed || (a.expandsParam >= 0 && a.pack.size() - 1 < fixed)) return false;
                    for (std::size_t k = 0; k < fixed; k++)
                        if (p.packIsValue ? p.pack[k]->kind() != Kind::TemplateParam &&
                                                p.packValues[k] != a.packValues[k]
                                          : !matchPattern(p.pack[k], a.pack[k], binding, why))
                            return false;
                    const std::size_t which = static_cast<std::size_t>(p.expandsParam);
                    if (matchPacks_ != nullptr && which < matchPacks_->size()) {
                        PackMembers got(std::vector<const Type *>(a.pack.begin() + static_cast<long>(fixed),
                                                                  a.pack.end()));
                        if (a.packIsValue)
                            got.values.assign(a.packValues.begin() + static_cast<long>(fixed), a.packValues.end());
                        (*matchPacks_)[which] = got;
                    }
                    continue;
                }
                if (a.expandsParam >= 0 || p.pack.size() != a.pack.size()) return false;
                if (p.packIsValue) {
                    for (std::size_t k = 0; k < p.pack.size(); k++)
                        if (p.pack[k]->kind() != Kind::TemplateParam && p.packValues[k] != a.packValues[k])
                            return false;
                    continue;
                }
                for (std::size_t k = 0; k < p.pack.size(); k++)
                    if (!matchPattern(p.pack[k], a.pack[k], binding, why)) return false;
                continue;
            }
            if (!p.isType) {
                // Two expressions are one argument only as one interned node: [temp.over.link]/5.
                if (p.expr != nullptr || a.expr != nullptr) {
                    if (p.expr != a.expr) return false;
                    continue;
                }
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

// `R(Args...)` as a template argument, [temp.arg.type]: a function type, its parameters read as a declarator's.
const Type *Parser::functionTypeArgument(const Type *base) {
    std::vector<const Type *> params;
    bool variadic = false;
    parameterTypes(params, variadic);
    return types_.functionType(base, std::move(params), variadic);
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

    Bound bound(this);
    std::vector<Shadow> &undo = bound.undo;
    std::vector<const Type *> binding(ps->params.size());
    std::vector<long long> values(ps->params.size(), 1);
    // Every parameter stands for itself - a non-type one too, so `T[N]` reads N as the bound it binds.
    for (std::size_t i = 0; i < ps->params.size(); i++)
        binding[i] = types_.templateParam(static_cast<int>(i));
    bindTemplateParameters(ps->params, binding, values,
                           Packs(), &undo);

    for (std::size_t i = 0; variadic ? !atClosingAngle() : i < count; i++) {
        // `Tr<Box<T> >` of `Tr<class T, class E = void>`: the arguments left out are the primary's defaults, as in
        // any template-id ([temp.arg]/4; 1.82: "expected ','"). Read where they were written.
        if (!variadic && i > 0 && atClosingAngle() && primary[i].defBegin != 0) {
            const std::size_t resume = at_;
            for (std::size_t k = i; k < count; k++) {
                if (primary[k].defBegin == 0)
                    src_.fail(peek().pos, "this specialization gives fewer arguments than the template has "
                                          "parameters, and parameter " + std::to_string(k + 1) + " has no default");
                TemplateDecl::Partial::Arg d;
                at_ = primary[k].defBegin;
                if (primary[k].type == nullptr && !primary[k].isTemplate) {
                    StorageClass sc;
                    Qualifiers quals;
                    const Type *base = specifiers(&sc, &quals);
                    d.isType = true;
                    d.type = declarator(base, true).type;
                } else if (primary[k].type != nullptr) {
                    d.isType = false;
                    d.value = constantExpression("a template argument");
                } else {
                    src_.fail(peek().pos, "a template template parameter's default left out of a partial "
                                          "specialization is not supported yet - write it");
                }
                ps->args.push_back(d);
            }
            at_ = resume;
            break;
        }
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
        // **One of our own template template parameters, passed on** - `Any<Trait, A, R...>`: it stands
        // for the template it will be bound to, as a type parameter stands for its type.
        if (peek().kind == TokenKind::Ident && (peekAt(1).is(",") || peekAt(1).is(">") || peekAt(1).is(">>"))) {
            std::size_t tt = ps->params.size();
            for (std::size_t k = 0; k < ps->params.size(); k++)
                if (ps->params[k].isTemplate && ps->params[k].name == peek().text) tt = k;
            if (tt < ps->params.size()) {
                a.isType = true;
                a.type = types_.templateParam(static_cast<int>(tt));
                at_++;
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
            Declared d;
            if (peek().is("(") && !atParenDeclarator()) d.type = functionTypeArgument(base);
            else d = declarator(base, true);
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

    bound.release();
    inTemplateArgs_ = wasInArgs;
    patternOnly_ = wasPattern;
}

// [temp.class.order]: A is as specialized as B when B's pattern matches A's,
// A's parameters opaque; B's pack expansion ([temp.deduct.type]/9) takes all
// A has left and may be empty; one argument of B's never takes A's pack.
bool Parser::atLeastAsSpecialized(const TemplateDecl::Partial &a,
                                  const TemplateDecl::Partial &b) const {
    std::vector<const Type *> binding(b.params.size());
    std::string why;
    // B's bounds and packs bind as a match would bind them, into lists nobody reads.
    std::vector<long long> values(b.params.size(), 0);
    Packs packs(b.params.size());
    struct Restore {
        const Parser *p; std::vector<long long> *v; Packs *k;
        ~Restore() { p->matchValues_ = v; p->matchPacks_ = k; }
    } restore{ this, matchValues_, matchPacks_ };
    matchValues_ = &values;
    matchPacks_ = &packs;
    // A value parameter written twice in B takes one thing: A's value, or one parameter of A's.
    std::vector<int> took(b.params.size(), 0);
    std::vector<long long> as(b.params.size(), 0);
    for (std::size_t i = 0; ; i++) {
        if (i < b.args.size() && b.args[i].isPackExpansion) {
            const Type *pattern = b.args[i].type;
            if (pattern == nullptr || pattern->kind() == Kind::TemplateParam) return true;
            for (std::size_t k = i; k < a.args.size(); k++)
                if (!a.args[k].isType || a.args[k].type == nullptr ||
                    !matchPattern(pattern, a.args[k].type, &binding, &why))
                    return false;
            return true;
        }
        if (i >= b.args.size() && i + 1 == a.args.size() && a.args[i].isPackExpansion) return true;
        if (i >= a.args.size() || i >= b.args.size()) return a.args.size() == b.args.size();
        if (a.args[i].isPackExpansion) return false;
        if (a.args[i].isType != b.args[i].isType) return false;
        if (!a.args[i].isType) {
            if (b.args[i].isParam) {              // a parameter takes anything, the same each time
                const std::size_t j = b.args[i].param;
                const int kind = a.args[i].isParam ? 2 : 1;
                const long long what = a.args[i].isParam ? static_cast<long long>(a.args[i].param) : a.args[i].value;
                if (j < took.size()) {
                    if (took[j] != 0 && (took[j] != kind || as[j] != what)) return false;
                    took[j] = kind;
                    as[j] = what;
                }
                continue;
            }
            if (a.args[i].isParam) return false;
            if (a.args[i].value != b.args[i].value) return false;
            continue;
        }
        if (!matchPattern(b.args[i].type, a.args[i].type, &binding, &why))
            return false;
    }
}

bool Parser::moreSpecialized(const TemplateDecl::Partial &a,
                             const TemplateDecl::Partial &b) const {
    const bool ab = atLeastAsSpecialized(a, b);
    const bool ba = atLeastAsSpecialized(b, a);
    if (ab && !ba) return true;
    // **[temp.deduct.partial]/11 (CWG 1395, which clang applies to C++11)**: each at least as
    // specialized as the other, the one without a trailing pack is the more specialized -
    // `L<T>` over `L<T, Ts...>` for `L<int>`.
    if (ab && ba) {
        const bool aPack = !a.args.empty() && a.args.back().isPackExpansion;
        const bool bPack = !b.args.empty() && b.args.back().isPackExpansion;
        return bPack && !aPack;
    }
    return false;
}

// Which partial specialization these arguments ask for.
std::size_t Parser::choosePartial(const TemplateDecl &decl,
                                  const std::vector<TemplateArg> &args,
                                  std::vector<const Type *> *binding,
                                  std::vector<long long> *values,
                                  Packs *packs,
                                  std::size_t pos) {
    std::vector<std::size_t> fits;
    Packs bindings;
    std::vector<std::vector<long long> > valueSets;
    std::vector<Packs > packSets;

    // **The arguments arrive as one pack when the primary is variadic.**
    // `L<int, char>` against `template <class... Ts>` is a single TemplateArg
    // holding both, so the list is flattened here and every pattern matched on it.
    std::vector<TemplateArg> flat;
    for (std::size_t i = 0; i < args.size(); i++) {
        if (!args[i].isPack) { flat.push_back(args[i]); continue; }
        for (std::size_t k = 0; k < args[i].pack.size(); k++) {
            TemplateArg one;
            one.isType = !args[i].packIsValue;
            one.type = args[i].pack[k];
            if (args[i].packIsValue) one.value = args[i].packValues[k];
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
        Packs pk(ps.params.size());
        std::string why;
        bool ok = true;
        struct Bounds {
            const Parser *p;
            ~Bounds() { p->matchValues_ = nullptr; p->matchPacks_ = nullptr; }
        } bounds{ this };
        matchValues_ = &v;
        matchPacks_ = &pk;
        // [temp.class.spec.match]/2: a parameter written twice deduces one value - `C<N, N>` is no `C<0, 2>`.
        std::vector<bool> valued(ps.params.size(), false);
        for (std::size_t i = 0; i < fixed && ok; i++) {
            const TemplateDecl::Partial::Arg &a = ps.args[i];
            if (a.isType != flat[i].isType) { ok = false; break; }
            if (!a.isType) {
                if (a.isParam) {
                    if ((valued[a.param] || b[a.param] != nullptr) && v[a.param] != flat[i].value) ok = false;
                    v[a.param] = flat[i].value;
                    valued[a.param] = true;
                } else if (a.value != flat[i].value) {
                    ok = false;
                }
                continue;
            }
            if (!matchPattern(a.type, flat[i].type, &b, &why)) ok = false;
        }
        if (ok && takesRest) {
            const TemplateDecl::Partial::Arg &tail = ps.args.back();
            const bool wantsValues = ps.params[tail.param].type != nullptr;
            for (std::size_t i = fixed; i < flat.size() && ok; i++) {
                if (flat[i].isType == wantsValues) { ok = false; break; }
                pk[tail.param].push_back(flat[i].type);
                if (wantsValues) pk[tail.param].values.push_back(flat[i].value);
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

// `a::b::` as the stack of namespaces it names, which is what namespaceStack_ holds.
std::vector<std::string> Parser::namespacePathStack(const std::string &ns) {
    std::vector<std::string> stack;
    for (std::size_t s = 0, e; s < ns.size(); s = e + 2) {
        e = ns.find("::", s);
        if (e == std::string::npos) e = ns.size();
        if (e > s) stack.push_back(ns.substr(s, e - s));
    }
    return stack;
}

const Parser::TemplateDecl *Parser::memberClassTemplateOf(const Type *cls, const std::string &name) const {
    if (cls == nullptr) return nullptr;
    cls = cls->unqualified();
    if (!cls->isStructOrUnion() || cls->tag().empty()) return nullptr;
    const std::map<std::string, TemplateDecl>::const_iterator it = templates_.find(cls->tag() + "::" + name);
    if (it != templates_.end() && it->second.isClass) return &it->second;
    for (const Type::BaseSpec *b : cls->directBases())
        if (const TemplateDecl *d = memberClassTemplateOf(b->type, name)) return d;
    return nullptr;
}

const Parser::TemplateDecl *Parser::memberClassTemplate(const std::string &name) const {
    std::vector<const Type *> scopes(classStack_.rbegin(), classStack_.rend());
    if (currentClass_ != nullptr) scopes.push_back(currentClass_);
    if (const Type *scope = lambdaScope()) scopes.push_back(scope);
    // A held body's return type is read before anything says whose member it is - classScopeType's reason.
    if (!inlineOwner_.empty()) {
        const auto it = typedefIndex_.find(inlineOwner_);
        if (it != typedefIndex_.end()) scopes.push_back(typedefs_[it->second].type);
    }
    for (const Type *c : scopes)
        for (const Type *e = c; e != nullptr; e = e->enclosing())
            if (const TemplateDecl *d = memberClassTemplateOf(e, name)) {
                // [temp.dep]/3: not from a base written with a template parameter.
                const Type *owner = ownerTypeOf(*d);
                if (owner != nullptr && e->unqualified() != owner->unqualified() && inDependentBase(e, owner))
                    continue;
                return d;
            }
    return nullptr;
}

const Type *Parser::ownerTypeOf(const TemplateDecl &decl) const {
    const auto o = typedefIndex_.find(decl.ownerTag);
    return o == typedefIndex_.end() ? nullptr : typedefs_[o->second].type;
}

void Parser::checkMemberTemplateAccess(const TemplateDecl &decl, std::size_t pos) const {
    if (decl.memberAccess == Access::Public) return;
    const Type *owner = ownerTypeOf(decl);
    if (owner == nullptr || insideClass(owner)) return;
    struct Walk {
        static bool derives(const Type *from, const Type *to, int depth) {
            if (from->unqualified() == to->unqualified()) return true;
            if (depth > 32) return false;
            for (const Type::BaseSpec &s : from->unqualified()->bases())
                if (derives(s.type, to, depth + 1)) return true;
            return false;
        }
    };
    if (decl.memberAccess == Access::Protected) {
        std::vector<const Type *> scopes(classStack_.begin(), classStack_.end());
        if (currentClass_ != nullptr) scopes.push_back(currentClass_);
        for (const Type *c : scopes)
            for (const Type *e = c; e != nullptr; e = e->enclosing())
                if (Walk::derives(e, owner, 0)) return;
    }
    src_.fail(pos, "'" + decl.name.substr(decl.ownerTag.size() + 2) + "' is " +
                   (decl.memberAccess == Access::Private ? "private" : "protected") + " in '" + owner->tag() + "'");
}

bool Parser::qualifierDependsOnParameter(std::size_t colon) const {
    if (colon < 1) return false;
    struct Bound {
        const Parser *p;
        bool operator()(const Token &t) const {
            return t.kind == TokenKind::Ident &&
                   (p->boundTypeParams_.count(t.text) != 0 || p->boundValueParams_.count(t.text) != 0 ||
                    p->packs_.count(t.text) != 0);
        }
    } bound{ this };
    if (bound(tokens_[colon - 1])) return true;
    if (!tokens_[colon - 1].is(">") && !tokens_[colon - 1].is(">>")) return false;
    int depth = 0;
    for (std::size_t k = colon - 1; k > 0; k--) {
        const Token &t = tokens_[k];
        if (t.is(">")) depth++;
        else if (t.is(">>")) depth += 2;
        else if (t.is("<") && --depth <= 0) break;
        else if (bound(t)) return true;
    }
    return false;
}

// `Box<int, 3>` where a type was expected - rung 5.4. The class is made by replaying
// `struct Box { ... };` with the arguments bound, and all the class path had to be
// told is what tag to take: nested classes had made tag() an arbitrary string.
const Type *Parser::instantiateClass(const TemplateDecl &decl, std::size_t pos) {
    // Before the arguments are read: `A<A<A<...>>>` instantiates from the inside out, through here.
    const Nesting deep(this, pos, "template instantiation");
    std::vector<const Type *> binding;
    std::vector<long long> values;
    std::vector<TemplateArg> args;
    Packs packs;
    // A typedef's own template-id may wait (below); what its arguments make - `default_delete<Formula>`, a default
    // of `unique_ptr<Formula>` - is made as it is read (1.82: deferred too, it stayed incomplete inside the
    // specialization that needed it, and `__cxx1_is_class` refused it).
    const bool typedefOuter = typedefDefer_;
    {
        struct Back {
            Parser *p; bool was;
            ~Back() { p->typedefDefer_ = was; }
        } back{ this, typedefDefer_ };
        typedefDefer_ = false;
        templateArguments(decl, &binding, &values, &args, &packs);
    }
    // [temp.inst]/1: a pointer or reference to a specialization does not need it complete. Named so inside another
    // specialization being read - `basic_ostream<C, Tr> *tie_` in basic_ios<char>, whose basic_ostream<char> has
    // basic_ios<char> for a base - it is put off until a use needs it, or the outermost class is done.
    if (classesInstantiating_ > 0 && decl.defined && !decl.isAlias && decl.ttParam < 0 && decl.ownerTag.empty() &&
        !dependentArgs(args) && (peek().is("*") || peek().is("&") || peek().is("&&"))) {
        const std::string tag = specializationKey(tagName(decl), args);
        if (findTypedef(tag) == nullptr) {
            Type *shallow = types_.structType(Kind::Struct, tag);
            if (!shallow->isSpecialization()) {
                shallow->setSpecialization(decl.name, args);
                shallow->setTemplateNamespace(decl.ns);
                shallow->noteClassKey(decl.classKey);
            }
            if (!shallow->isComplete()) {
                pendingSpecializations_[tag] = PendingSpecialization{ tagName(decl), binding, values, args, packs, true };
                return shallow;
            }
        }
    }
    // And a typedef's: with a class argument still incomplete, the specialization waits for a use that needs it
    // complete, by which time the argument may be ([temp.inst]/1; 1.82, `typedef std::pair<std::string, Value>`
    // before Value's definition).
    if (typedefOuter && classesInstantiating_ == 0 && decl.defined && !decl.isAlias && decl.ttParam < 0 &&
        decl.ownerTag.empty() && !dependentArgs(args)) {
        bool incomplete = false;
        for (const TemplateArg &a : args) {
            if (!a.isType || a.isTemplate) continue;
            std::vector<const Type *> each;
            if (a.isPack) each = a.pack; else each.push_back(a.type);
            for (const Type *t : each)
                if (t != nullptr && t->unqualified()->isStructOrUnion() && !t->unqualified()->isComplete())
                    incomplete = true;
        }
        const std::string tag = specializationKey(tagName(decl), args);
        if (incomplete && findTypedef(tag) == nullptr) {
            Type *shallow = types_.structType(Kind::Struct, tag);
            if (!shallow->isSpecialization()) {
                shallow->setSpecialization(decl.name, args);
                shallow->setTemplateNamespace(decl.ns);
                shallow->noteClassKey(decl.classKey);
            }
            if (!shallow->isComplete()) {
                if (pendingSpecializations_.count(tag) == 0)
                    pendingSpecializations_[tag] = PendingSpecialization{ tagName(decl), binding, values, args, packs, false };
                return shallow;
            }
        }
    }
    return instantiateClassWith(decl, binding, values, args, packs, pos);
}

void Parser::instantiateDeferredSpecializations(std::size_t pos) {
    for (;;) {
        std::map<std::string, PendingSpecialization>::iterator it = pendingSpecializations_.begin();
        while (it != pendingSpecializations_.end() && !it->second.deferred) ++it;
        if (it == pendingSpecializations_.end()) return;
        const PendingSpecialization held = it->second;
        pendingSpecializations_.erase(it);
        const auto d = templates_.find(held.templateName);
        if (d == templates_.end()) continue;
        const TemplateDecl decl = d->second;
        instantiateClassWith(decl, held.binding, held.values, held.args, held.packs, pos);
    }
}

const Type *Parser::instantiateClassWith(const TemplateDecl &decl, const std::vector<const Type *> &binding,
                                         const std::vector<long long> &values, const std::vector<TemplateArg> &args,
                                         const Packs &packs, std::size_t pos) {
    // **A member template of a class template binds the class's parameters too**, as they were where it was
    // declared - after its own arguments are read, which name the parameters of whatever wrote them.
    const bool member = !decl.ownerTag.empty();
    std::vector<Shadow> classUndo;
    struct UnbindClass {
        Parser *p; std::vector<Shadow> *u;
        ~UnbindClass() { p->unbindTemplateParameters(*u); }
    } unbindClass{ this, &classUndo };
    if (member) bindTemplateParameters(decl.classParams, decl.classBinding, decl.classValues, decl.classPacks, &classUndo);

    // [temp.alias]/2: an alias template's id is its type-id with the arguments substituted.
    if (decl.isAlias) {
        const std::size_t resume = at_;
        std::vector<Shadow> undo;
        struct Unbind {
            Parser *p; std::vector<Shadow> *u;
            ~Unbind() { p->unbindTemplateParameters(*u); }
        } unbind{ this, &undo };
        bindTemplateParameters(decl.params, binding, values, packs, &undo);
        // An alias's type-id means what it meant where it was written.
        struct BackTo {
            std::vector<std::string> &stack; std::vector<std::string> was;
            ~BackTo() { stack = was; }
        } backTo{ namespaceStack_, namespaceStack_ };
        if (!decl.ns.empty()) namespaceStack_ = namespacePathStack(decl.ns);
        at_ = decl.aliasAt;
        StorageClass sc;
        Qualifiers quals;
        const Type *base = specifiers(&sc, &quals);
        const Type *t = declarator(base, true).type;
        at_ = resume;
        return t;
    }
    const std::string tag = specializationKey(tagName(decl), args);
    // A member class template's specialization is a class nested in its owner, named by the template's own name.
    const std::string bare = member ? decl.name.substr(decl.ownerTag.size() + 2) : decl.name;
    const Type *owner = nullptr;
    if (member) owner = ownerTypeOf(decl);

    // **A pattern's `C<T>`**, the stand-in applied: a shape deduction matches and Itanium spells `T_I...E`.
    if (decl.ttParam >= 0) {
        Type *shallow = types_.structType(Kind::Struct, tag);
        if (!shallow->isSpecialization()) shallow->setSpecialization(decl.name, args);
        declareTypeName(tag, shallow);
        return shallow;
    }

    // Reading a pattern, not building a class: `Holder<T>` cannot be laid out, and the mangler
    // and deduction read only its name and arguments. An argument list that mentions no
    // parameter is a real class - `enable_if<2 + 2 == 4, int>`.
    if (patternOnly_ && dependentArgs(args)) {
        Type *shallow = types_.structType(Kind::Struct, tag);
        if (!shallow->isSpecialization()) {
            shallow->setSpecialization(bare, args);
            if (owner != nullptr) {
                shallow->setLocalName(bare);
                shallow->setEnclosing(owner);
            } else {
                shallow->setTemplateNamespace(decl.ns);
            }
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
    Packs usePacks;
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
        if (!member && !dependentArgs(args) && pendingSpecializations_.count(tag) == 0)
            pendingSpecializations_[tag] = PendingSpecialization{ tagName(decl), binding, values, args, packs, false };
        return shallow;
    }

    const std::size_t resume = at_;
    Bound bound(this);
    std::vector<Shadow> &undo = bound.undo;
    // **A chosen partial binds its own pack, not the primary's.** `L<T, R...>` on
    // `L<int, char, long>` leaves R holding {char, long}, which is what the replayed
    // body has to see - the primary's Ts is not in scope at all.
    bindTemplateParameters(useParams, useBinding, useValues,
                           partial ? usePacks : packs, &undo);

    at_ = partial ? decl.partials[which].bodyAt : decl.afterParams;
    // **A specialization is not a member of whatever class asked for it.**
    std::vector<const Type *> outerClasses;
    outerClasses.swap(classStack_);
    if (owner != nullptr) classStack_.push_back(owner);
    const Type *outerCurrent = currentClass_;
    currentClass_ = nullptr;
    const std::string outerInline = inlineOwner_;
    inlineOwner_.clear();
    // **The `>>` mark is one slot and this parse can spend it.**
    const std::size_t outerAngle = angleSplit_;

    // **The class being read around this one keeps its own**: a member template recorded after
    // `Vec<N>` in Mat's body would otherwise capture Vec's parameters as Mat's.
    const std::vector<TemplateArg> outerArgs = instantiatingArgs_;
    const std::string outerNamespace = instantiatingNamespace_;
    const auto outerParams = instantiatingParams_;
    const auto outerBinding = instantiatingBinding_;
    const auto outerValues = instantiatingValues_;
    // A member's tag is made below as its owner's and this one's, `S::In<int>`.
    classInstantiationTag_ = member ? specializationKey(bare, args) : tag;
    if (partial) classInstantiationOf_ = decl.name;
    instantiatingArgs_ = args;
    instantiatingNamespace_ = member ? std::string() : decl.ns;
    std::vector<TemplateParam> allParams = decl.classParams;
    std::vector<const Type *> allBinding = decl.classBinding;
    std::vector<long long> allValues = decl.classValues;
    allBinding.resize(allParams.size());
    allValues.resize(allParams.size());
    allParams.insert(allParams.end(), useParams.begin(), useParams.end());
    allBinding.insert(allBinding.end(), useBinding.begin(), useBinding.end());
    allValues.insert(allValues.end(), useValues.begin(), useValues.end());
    instantiatingParams_ = allParams;
    instantiatingBinding_ = allBinding;
    instantiatingValues_ = allValues;
    heldForSpecialization_.clear();
    const bool wasDeferring = deferSpecializationBodies_;
    deferSpecializationBodies_ = true;
    StorageClass sc;
    Qualifiers quals;
    // Nor in a class template's: `V<3> N(...)` made V<3> read its own `double s[N]` as the local.
    std::unique_ptr<LocalsHidden> hidden(new LocalsHidden(this));
    // And its body names things in its own namespace, as a function template's does, wherever it is used from.
    const std::vector<std::string> outerStack = namespaceStack_;
    if (!decl.ns.empty()) namespaceStack_ = namespacePathStack(decl.ns);
    struct Reading {
        int *n;
        explicit Reading(int *k) : n(k) { ++*n; }
        ~Reading() { --*n; }
    };
    std::unique_ptr<Reading> reading(new Reading(&classesInstantiating_));
    const InChain chain(src_, pos, tag);
    const Type *made = partial
        ? structOrUnionSpecifier(Kind::Struct, false)
        : specifiers(&sc, &quals);
    reading.reset();
    namespaceStack_ = outerStack;
    angleSplit_ = outerAngle;
    classInstantiationTag_.clear();
    classInstantiationOf_.clear();
    instantiatingArgs_ = outerArgs;
    instantiatingNamespace_ = outerNamespace;
    instantiatingParams_ = outerParams;
    instantiatingBinding_ = outerBinding;
    instantiatingValues_ = outerValues;
    hidden.reset();
    classStack_.swap(outerClasses);
    currentClass_ = outerCurrent;
    inlineOwner_ = outerInline;
    deferSpecializationBodies_ = wasDeferring;
    std::vector<PendingBody> bodies;
    bodies.swap(heldForSpecialization_);

    bound.release();
    at_ = resume;

    if (!made->isStructOrUnion())
        src_.fail(pos, "'" + decl.name + "' is not a class template");
    declareTypeName(tag, made);
    if (member) types_.structType(made->kind(), made->tag())->setNestedAccess(decl.memberAccess);

    Specialization sp;
    sp.key = tag;
    sp.name = decl.name;
    sp.templateKey = tagName(decl);
    sp.fromPartial = partial;
    sp.params = allParams;
    sp.binding = allBinding;
    sp.values = allValues;
    // **A partial's pack has to be recorded too.** Held bodies are replayed later
    // from this record, and one saying `Tuple<Rest...>` needs Rest as it was. Left
    // empty, `Tuple<char,long>::tail` returned `Tuple<long> &` and said `Tuple<> &`.
    sp.packs = partial ? usePacks : packs;
    if (member) {
        Packs owner = decl.classPacks;
        owner.resize(decl.classParams.size());
        if (sp.packs.empty()) sp.packs.resize(useParams.size());
        sp.packs.insert(sp.packs.begin(), owner.begin(), owner.end());
    }
    sp.start = decl.afterParams;
    sp.pos = pos;
    sp.chain = src_.instantiations();
    sp.isClass = true;
    sp.bodies = bodies;
    // Its member bodies, replayed later, ask from one deeper than the body that asked for the class.
    sp.depth = instantiationDepth_ + 1;
    refuseInstantiationDepth(decl.name, sp.depth, pos);
    specializations_.push_back(sp);
    if (classesInstantiating_ == 0) instantiateDeferredSpecializations(pos);
    return made;
}

// The function type a target names - itself, or what a pointer or reference to it points at - or null.
static const Type *targetFunction(const Type *target) {
    const Type *t = target == nullptr ? nullptr : target->unqualified();
    if (t != nullptr && t->isReference()) t = t->referent()->unqualified();
    if (t != nullptr && t->isFunctionPointer()) t = t->pointee();
    return t != nullptr && t->isFunction() ? t : nullptr;
}

std::vector<std::size_t> Parser::specializationsFor(const std::vector<TemplateDecl> &templates, const Type *target,
                                                    std::size_t pos) {
    std::vector<std::size_t> made;
    const Type *fn = targetFunction(target);
    if (fn == nullptr) return made;
    for (const TemplateDecl &d : templates) {
        std::vector<const Type *> binding;
        std::vector<long long> values;
        Packs packs;
        std::string why, symbol;
        try {
            Trial trial(this);
            if (!deduceTemplateArguments(d, fn->params(), &binding, &values, &packs, &why)) continue;
            const std::vector<TemplateArg> args = argumentsFromBinding(d.params, binding, values, packs);
            symbol = instantiate(d, binding, values, args, pos, packs).symbol;
        } catch (const SubstitutionFailure &f) {
            if (f.unsupported) src_.fail(f.pos, f.why);
            continue;
        }
        for (std::size_t i = 0; i < functions_.size(); i++)
            if (functions_[i].symbol == symbol) { made.push_back(i); break; }
    }
    return made;
}

ExprPtr Parser::templateAsValue(const std::string &name, const std::string &scope, std::size_t pos) {
    std::string named;
    const std::vector<TemplateDecl> reached = templatesNamed(name, scope, &named);
    // The ordinary functions of the name are candidates beside the specializations, [over.over]/4 preferring them:
    // those of the template's own namespace first, as a call takes them - `float (*)(float) = std::sqrt`.
    std::vector<std::size_t> set;
    const std::string bare = name.substr(name.rfind("::") == std::string::npos ? 0 : name.rfind("::") + 2);
    const std::vector<std::size_t> *had = !named.empty() && named != "::" ? overloadsOf(named + bare) : nullptr;
    if (had == nullptr) had = overloadsOf(qualifyForLookup(name, &Parser::hasFunctionNamed));
    if (had != nullptr)
        for (std::size_t i : *had)
            if (!functions_[i].fromTemplate) set.push_back(i);
    if (targetFunction(overloadTarget_) != nullptr) {
        const Type *target = overloadTarget_;
        const Signature *plain = pickByTarget(set, nullptr, target);
        if (plain == nullptr) {
            const std::vector<std::size_t> made = specializationsFor(reached, target, pos);
            set.insert(set.end(), made.begin(), made.end());
            plain = pickByTarget(set, nullptr, target);
        }
        if (plain == nullptr)
            src_.fail(pos, "'" + name + "' is a function template, and none of its specializations is of the "
                           "type '" + target->describe() + "' wanted here");
        markUsed(plain);
        const Signature chosen = *plain;
        return functionAddress(chosen, name);
    }
    if (overloadArgument_) {
        ExprPtr mark = overloadPlaceholder(set, name, nullptr, pos);
        overloadArgs_.back().templates = reached;
        return mark;
    }
    src_.fail(pos, "'" + name + "' is a function template, and named without a call where no function type is "
                   "wanted its specialization cannot be worked out - [temp.deduct.funcaddr]");
    return nullptr;
}

ExprPtr Parser::shiftedFunction(const Type *left, BinOp op, ExprPtr rhs) {
    const OverloadArg *oa = overloadArgOf(*rhs);
    if (oa == nullptr) return rhs;
    const OverloadArg held = *oa;
    const std::string name = std::string("operator") + binOpSpelling(op);
    const Type *owner = findMemberOwner(left->unqualified(), name);
    const std::vector<std::size_t> *set = owner != nullptr ? overloadsOf(owner->tag() + "::" + name) : nullptr;
    std::vector<const Type *> wanted;
    if (set != nullptr)
        for (std::size_t i : *set)
            if (functions_[i].params.size() == 1 && targetFunction(functions_[i].params[0]) != nullptr)
                wanted.push_back(functions_[i].params[0]);
    for (const Type *param : wanted)
        if (ExprPtr picked = overloadArgumentFor(param, held)) return picked;
    src_.fail(held.pos, "'" + held.key + "' names more than one function, and no '" + name + "' of '" +
                        left->describe() + "' takes a pointer to one of them");
    return nullptr;
}

// A template named in an expression. 5.2 wants the arguments written out:
// deducing them from the call is 5.3, and a class template is 5.4.
ExprPtr Parser::templateCall(Program *program, const std::string &scope) {
    const std::string name = peek().text;
    const std::size_t pos = peek().pos;
    TemplateDecl decl = templateEntry(scope.empty() ? name : scope + "::" + name)->second;
    // `la::rank(x)` names la's function template, though another namespace's class template `rank` holds the
    // table's entry for the name (1.82: "'rank' is a class template", std::rank beside a library's own).
    if (decl.isClass && !scope.empty() && decl.ns != scope + "::") {
        const auto fns = fnTemplates_.find(name);
        if (fns != fnTemplates_.end())
            for (const TemplateDecl &d : fns->second)
                if (d.ns == scope + "::") { decl = d; break; }
    }
    // Unqualified the same: std::rank, which nothing brings here, does not hide a reachable function template
    // `rank` - the program's own, or one a using-directive opened (1.83).
    if (decl.isClass && scope.empty() && !templateReachable(decl, name)) {
        const auto fns = fnTemplates_.find(name);
        if (fns != fnTemplates_.end())
            for (const TemplateDecl &d : fns->second)
                if (templateReachable(d, name)) { decl = d; break; }
    }
    if (decl.isClass) {
        // [basic.lookup.unqual]: a namespace's class template named bare where nothing brings it - in an
        // expression as in a type (1.83: `is_same<int, int>::value` at global scope was std's, and a program's own
        // function template `rank` met std::rank).
        if (scope.empty() && peekAt(1).is("<") && !templateReachable(decl, name))
            src_.fail(pos, "'" + name + "' is a template of the namespace '" + decl.ns.substr(0, decl.ns.size() - 2) +
                           "', and nothing brings it here - [basic.lookup.unqual]; write '" + decl.ns + name +
                           "', or a using-declaration");
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
            if (cls != nullptr && cls->isStructOrUnion() && peek().is("{"))
                return listTemporary(cls, pos);
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
        // `X{r}` as well, list-initialised ([temp.local]/1; 1.82: "'X' is a class template, and instantiating one
        // is not supported yet").
        if (peek().is("{")) {
            if (const Type *self = findTypedef(name))
                if (self->isStructOrUnion() && self->isSpecialization() && self->templateName() == name)
                    return listTemporary(self, pos);
        }
        // **And before `::`**: `Tree::next(n)` inside Tree<T>'s members, its nested and derived
        // classes is the road `Tree<T>::next(n)` takes - [temp.local]/1. At file scope nothing
        // answers for the bare name and the refusal below stands.
        if (peek().is("::")) {
            if (const Type *self = findTypedef(name))
                if (self->isStructOrUnion() && self->isSpecialization() &&
                    self->templateName() == name) {
                    at_++;
                    return templateIdMember(self, pos);
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
        if (!peek().is("(")) return templateAsValue(name, scope, pos);
        at_++;
        std::vector<ExprPtr> callArgs;
        parseArguments(callArgs);

        std::vector<const Type *> argTypes;
        for (std::size_t i = 0; i < callArgs.size(); i++)
            argTypes.push_back(deductionArgType(*callArgs[i]));

        // **Every function template the name reaches is a candidate** - one namespace's, the nearest.
        std::string named;
        std::vector<TemplateDecl> reached = templatesNamed(name, scope, &named);
        // A block's `using std::sin;` is what ordinary lookup finds first ([basic.lookup.unqual]/1): its namespace's
        // functions, not the templates of the namespace around the block (1.82: `sin(x.re())` of a double in
        // ad::sin<T> was taken for ad::sin and refused). Argument-dependent lookup below adds the rest.
        {
            const auto bu = scope.empty() ? blockUsingFunctions_.find(name) : blockUsingFunctions_.end();
            if (bu != blockUsingFunctions_.end() && bu->second.rfind("::") != std::string::npos) {
                const std::string tns = bu->second.substr(0, bu->second.rfind("::") + 2);
                std::vector<TemplateDecl> mine;
                const auto ft = fnTemplates_.find(name);
                if (ft != fnTemplates_.end())
                    for (const TemplateDecl &d : ft->second)
                        if (d.ns == tns) mine.push_back(d);
                reached = mine;
                named = tns;
            }
        }
        std::set<std::string> deduced;
        instantiateViableTemplates(reached, argTypes, pos, &deduced);

        // Nothing deduced, no ordinary function: say why the template failed
        // (better than `not declared`; the first overload stands for all).
        // [temp.deduct]/8: a template that does not deduce only drops out.
        auto whyNot = [&]() {
            std::vector<const Type *> b;
            std::vector<long long> v;
            Packs p;
            std::string why;
            deduceTemplateArguments(decl, argTypes, &b, &v, &p, &why);
            // Deduction can succeed and the instantiation still fail, and then
            // there is no reason to print - so say that rather than end the
            // sentence on "and".
            if (why.empty())
                why = "no specialization of it could be made for these "
                      "arguments";
            src_.fail(pos, "'" + name + "' is a function template and " + why);
        };
        const std::string bare = name.substr(name.rfind("::") == std::string::npos ? 0 : name.rfind("::") + 2);
        if (overloadsOf(name) == nullptr && (named.empty() || named == "::" || overloadsOf(named + bare) == nullptr) &&
            overloadsOf(qualifyForLookup(name, &Parser::hasFunctionNamed)) == nullptr)
            whyNot();

        // **In the template's own namespace**, whose ordinary functions of this name are candidates
        // beside its specializations - [over.match.best]: an exact non-template beats a template.
        // The global namespace's is its ordinary functions and its own specializations, no other's.
        if (named == "::") {
            std::vector<std::size_t> mine;
            if (const std::vector<std::size_t> *all = overloadsOf(bare))
                for (std::size_t i : *all) if (!functions_[i].fromTemplate) mine.push_back(i);
            if (const std::vector<std::size_t> *own = overloadsOf("::" + bare))
                mine.insert(mine.end(), own->begin(), own->end());
            functionIndex_["$global:" + bare] = mine;
            named = "$global:";
        }
        std::string key = !named.empty() && overloadsOf(named + bare) != nullptr ? named + bare
                        : !decl.ns.empty() && overloadsOf(decl.ns + name) != nullptr
                        ? decl.ns + name : name;
        // **[basic.lookup.argdep]/1: an unqualified call adds what its arguments' namespaces declare** - their
        // ordinary functions and the specializations of their templates - so `swap(u, v)` finds user::swap
        // beside a std::swap<T>, and the non-template wins the tie.
        std::vector<std::size_t> joined;
        if (scope.empty() && name.find("::") == std::string::npos) {
            std::vector<std::string> spaces;
            for (const ExprPtr &a : callArgs) associatedNamespaces(a->type(), &spaces, 0);
            std::vector<TemplateDecl> more;
            for (const std::string &ns : spaces)
                for (const TemplateDecl &d : fnTemplates_[bare]) {
                    if (d.ns != (ns.empty() ? std::string() : ns + "::")) continue;
                    bool had = false;
                    for (const TemplateDecl &r : reached) had = had || (r.ns == d.ns && r.pos == d.pos);
                    if (!had) more.push_back(d);
                }
            instantiateViableTemplates(more, argTypes, pos, &deduced);
            if (const std::vector<std::size_t> *s = overloadsOf(key)) joined = *s;
            const std::size_t before = joined.size();
            for (const std::string &ns : spaces) {
                if (const std::vector<std::size_t> *s = overloadsOf(ns.empty() ? "::" + bare : ns + "::" + bare))
                    for (std::size_t i : *s)
                        if (std::find(joined.begin(), joined.end(), i) == joined.end()) joined.push_back(i);
                // The global namespace's ordinary functions are under the bare name (1.77: only its templates'
                // specializations were added, so a global class's own `swap(C &, C &)` lost to std::swap<C>).
                if (ns.empty())
                    if (const std::vector<std::size_t> *s = overloadsOf(bare))
                        for (std::size_t i : *s)
                            if (!functions_[i].fromTemplate && functions_[i].owner.empty() &&
                                std::find(joined.begin(), joined.end(), i) == joined.end())
                                joined.push_back(i);
            }
            if (joined.size() > before) {
                key = "$adl:" + bare;
                functionIndex_[key] = joined;
            }
        }
        // [temp.over]/1: a specialization is a candidate where this call deduced it - `f<int>`, which `f(1, 1)`
        // made, is none of `f(1, 2.0)`'s, whose T is deduced two types.
        if (const std::vector<std::size_t> *s = overloadsOf(key)) {
            std::vector<std::size_t> kept;
            for (std::size_t i : *s)
                if (!functions_[i].fromTemplate || deduced.count(functions_[i].symbol) != 0) kept.push_back(i);
            if (kept.empty()) whyNot();
            if (kept.size() != s->size()) {
                if (key.compare(0, 5, "$adl:") == 0) functionIndex_.erase(key);
                key = ownKey("$adl:" + bare);
                functionIndex_[key] = kept;
            }
        }
        // A braced argument is made for the parameter of the specialization it deduced (CWG 1591).
        const Signature sig = resolveOverloadBraced(key, callArgs, pos);
        if (key.compare(0, 5, "$adl:") == 0) functionIndex_.erase(key);
        // **A specialization's defaults, as every ordinary call reads them.**
        applyDefaults(sig, callArgs, pos);
        return completeCall(sig.name, sig.symbol, nullptr, sig.returns,
                            sig.params, sig.variadic, pos, std::move(callArgs));
    }

    // **Every function template of this name is a candidate here too.** The written
    // list is read once per overload - `forward<T>` has one on `type &` and one on
    // `type &&` - and what each makes is ranked against the call's arguments below.
    std::string named;
    std::vector<TemplateDecl> overloads = templatesNamed(name, scope, &named);
    if (overloads.empty()) overloads.push_back(decl);
    const std::size_t listAt = at_;
    std::size_t afterList = 0;
    std::vector<std::size_t> made;
    (void)program;
    // **[temp.arg.explicit]/3: a list that stops short leaves the rest to deduction** - `make<S>(1, 'c')`.
    // Those overloads wait for the call's arguments, read once all the lists are.
    struct Short { std::size_t k; std::vector<const Type *> types; std::vector<long long> values; };
    std::vector<Short> shortLists;
    for (std::size_t k = 0; k < overloads.size(); k++) {
        std::vector<const Type *> binding;
        std::vector<long long> values;
        std::vector<TemplateArg> args;
        Packs packs;
        const bool wasInArgs = inTemplateArgs_;
        std::string symbol;
        at_ = listAt;
        try {
            Trial trial(this);
            std::size_t given = static_cast<std::size_t>(-1);
            templateArguments(overloads[k], &binding, &values, &args, &packs, &given);
            afterList = at_;
            if (given != static_cast<std::size_t>(-1)) {
                shortLists.push_back(Short{ k, std::vector<const Type *>(binding.begin(), binding.begin() + given),
                                            std::vector<long long>(values.begin(), values.begin() + given) });
                continue;
            }
            symbol = instantiate(overloads[k], binding, values, args, pos, packs).symbol;
        } catch (const SubstitutionFailure &f) {
            inTemplateArgs_ = wasInArgs;
            if (f.unsupported) src_.fail(f.pos, f.why);
            continue;
        }
        for (std::size_t i = 0; i < functions_.size(); i++)
            if (functions_[i].symbol == symbol) { made.push_back(i); break; }
    }
    std::vector<ExprPtr> callArgs;
    bool argsRead = false;
    if (!shortLists.empty()) {
        at_ = afterList;
        if (!peek().is("("))
            src_.fail(peek().pos, "'" + name + "' is a function template, and naming one without calling it "
                                  "is not supported yet");
        at_++;
        parseArguments(callArgs);
        argsRead = true;
        const std::size_t afterCall = at_;
        std::vector<const Type *> argTypes;
        for (const ExprPtr &a : callArgs) argTypes.push_back(deductionArgType(*a));
        for (const Short &s : shortLists) {
            const TemplateDecl &d = overloads[s.k];
            std::vector<const Type *> binding;
            std::vector<long long> values;
            Packs packs;
            std::string why;
            std::string symbol;
            try {
                Trial trial(this);
                if (!deduceTemplateArguments(d, argTypes, &binding, &values, &packs, &why, &s.types, &s.values))
                    continue;
                const std::vector<TemplateArg> args = argumentsFromBinding(d.params, binding, values, packs);
                symbol = instantiate(d, binding, values, args, pos, packs).symbol;
            } catch (const SubstitutionFailure &f) {
                if (f.unsupported) src_.fail(f.pos, f.why);
                continue;
            }
            for (std::size_t i = 0; i < functions_.size(); i++)
                if (functions_[i].symbol == symbol) { made.push_back(i); break; }
        }
        at_ = afterCall;
        if (made.empty())
            src_.fail(pos, "'" + name + "' has no specialization for these template arguments and this call's");
    }
    // Nothing applied: read the one template outside the trial, so its own diagnostic is printed.
    if (made.empty()) {
        at_ = listAt;
        std::vector<const Type *> binding;
        std::vector<long long> values;
        std::vector<TemplateArg> args;
        Packs packs;
        templateArguments(decl, &binding, &values, &args, &packs);
        instantiate(decl, binding, values, args, pos, packs);
        src_.fail(pos, "'" + name + "' has no specialization for these template arguments");
    }
    if (!argsRead) {
        at_ = afterList;
        // `twice<int>` named and not called: the specialization as a value, chosen by the type wanted if several.
        if (!peek().is("(")) {
            const Signature *one = made.size() == 1 ? &functions_[made[0]]
                                                    : pickByTarget(made, nullptr, overloadTarget_);
            if (one == nullptr && overloadArgument_) return overloadPlaceholder(made, name, nullptr, pos);
            if (one == nullptr)
                src_.fail(pos, "'" + name + "' with these template arguments names " + std::to_string(made.size()) +
                               " functions, and nothing here says which one is wanted");
            markUsed(one);
            const Signature chosen = *one;
            return functionAddress(chosen, name);
        }
        at_++;
        parseArguments(callArgs);
    }

    // One candidate is the answer; several are ranked under a key of their own, so nothing
    // else of this name joins. **A copy, not a reference**: parsing can move `functions_`.
    Signature sig = functions_[made[0]];
    if (made.size() > 1) {
        const std::string key = "$explicit:" + name;
        functionIndex_[key] = made;
        sig = resolveOverload(key, callArgs, pos);
        functionIndex_.erase(key);
    } else {
        // What resolveOverload counts for the call it chooses: `noexcept(f<T>())` asks (R20).
        if (!sig.isNoexcept) mayThrow_++;
        buildBracedArguments(sig.params, callArgs, pos);
    }
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
    // The class whose scope declares it - `d.twice(3)` finds B's template through a D.
    const Type *named = obj->unqualified();
    const Type *plain = memberTemplateOwner(named, name);
    if (plain == nullptr) plain = named;
    const std::vector<TemplateDecl> templates = memberTemplates_[plain->tag() + "::" + name];
    TemplateDecl mt = templates.front();
    // [class.access.base]/5: the member's own access, and every base on the way from the class it was named in.
    if (!insideAccessOf(plain, mt.memberAccess) && !isFriendOf(plain) &&
        !memberReachable(named, plain, mt.memberAccess))
        src_.fail(pos, "'" + name + "' is a member template of '" + plain->describe() +
                       "' that is not accessible here" +
                       (plain != named ? " through '" + named->describe() + "'" : std::string()));

    std::vector<const Type *> binding;
    std::vector<long long> values;
    std::vector<TemplateArg> args;
    Packs packs;
    std::vector<ExprPtr> deducedArgs;
    bool deduced = false;
    // Every member template is a candidate ([temp.deduct]/8); misfits drop
    std::vector<std::size_t> made;

    if (peek().is("<")) {
        const std::size_t listAt = at_;
        std::size_t afterList = listAt;
        // [temp.arg.explicit]/3: a list that stops short leaves the rest to the call's arguments -
        // `b.to_string<char>('.', '#')` - deduced once every list is read, as for a function template.
        struct Short { std::size_t t; std::vector<const Type *> types; std::vector<long long> values; };
        std::vector<Short> shortLists;
        for (std::size_t t = 0; t < templates.size(); t++) {
            at_ = listAt;
            std::vector<const Type *> b;
            std::vector<long long> v;
            std::vector<TemplateArg> a;
            Packs p;
            try {
                Trial trial(this);
                std::size_t given = static_cast<std::size_t>(-1);
                templateArguments(templates[t], &b, &v, &a, &p, &given);
                afterList = at_;
                if (given != static_cast<std::size_t>(-1)) {
                    shortLists.push_back(Short{ t, std::vector<const Type *>(b.begin(), b.begin() + given),
                                                std::vector<long long>(v.begin(), v.begin() + given) });
                    continue;
                }
                if (const Signature *s = instantiateMemberTemplate(templates[t], b, v, a, pos)) {
                    const std::size_t index = static_cast<std::size_t>(s - &functions_[0]);
                    if (std::find(made.begin(), made.end(), index) == made.end()) made.push_back(index);
                }
            } catch (const SubstitutionFailure &f) {
                if (f.unsupported) src_.fail(f.pos, f.why);
            }
        }
        if (!shortLists.empty()) {
            at_ = afterList;
            expect("(");
            parseArguments(deducedArgs);
            deduced = true;
            afterList = at_;
            std::vector<const Type *> argTypes;
            for (std::size_t i = 0; i < deducedArgs.size(); i++) argTypes.push_back(deductionArgType(*deducedArgs[i]));
            for (const Short &s : shortLists) {
                const TemplateDecl &d = templates[s.t];
                std::vector<const Type *> b;
                std::vector<long long> v;
                Packs p;
                std::string why;
                try {
                    Trial trial(this);
                    Bound bound(this);
                    if (!d.classParams.empty())
                        bindTemplateParameters(d.classParams, d.classBinding, d.classValues, d.classPacks, &bound.undo);
                    const bool ok = deduceTemplateArguments(d, argTypes, &b, &v, &p, &why, &s.types, &s.values);
                    bound.release();
                    if (!ok) continue;
                    const std::vector<TemplateArg> a = argumentsFromBinding(d.params, b, v, p);
                    if (const Signature *sig = instantiateMemberTemplate(d, b, v, a, pos)) {
                        const std::size_t index = static_cast<std::size_t>(sig - &functions_[0]);
                        if (std::find(made.begin(), made.end(), index) == made.end()) made.push_back(index);
                    }
                } catch (const SubstitutionFailure &f) {
                    if (f.unsupported) src_.fail(f.pos, f.why);
                }
            }
            if (made.empty())
                src_.fail(pos, "'" + plain->describe() + "::" + name + "' has no specialization for these "
                               "template arguments and this call's");
        }
        if (made.empty()) {
            at_ = listAt;
            templateArguments(mt, &binding, &values, &args, &packs);
            instantiateMemberTemplate(mt, binding, values, args, pos);
            src_.fail(pos, "'" + plain->describe() + "::" + name + "' has no specialization for these "
                           "template arguments");
        }
        at_ = afterList;
    } else {
        // **No argument list, so they come from the call** - the same order the
        // free-function path uses: the arguments are parsed first and the
        // parameters worked out from their types, [temp.deduct.call].
        expect("(");
        parseArguments(deducedArgs);
        deduced = true;
        std::vector<const Type *> argTypes;
        for (std::size_t i = 0; i < deducedArgs.size(); i++)
            argTypes.push_back(deductionArgType(*deducedArgs[i]));
        // **[temp.over]/1: every template of the name that deduces is a candidate**, and the call ranks them -
        // `add(It, It, Tag<false>)` and `add(It, It, Tag<true>)` both deduce It, and the third argument chooses.
        for (std::size_t t = 0; t < templates.size(); t++) {
            const std::size_t from = candidateSpecializations_.size();
            instantiateViableMemberTemplate(templates[t], argTypes, pos);
            // Its own specializations only: what its body called was listed after it too.
            for (std::size_t k = from; k < candidateSpecializations_.size(); k++) {
                const Signature &f = functions_[candidateSpecializations_[k]];
                if (f.owner != templates[t].ownerTag) continue;
                if (f.name != name && f.name.compare(0, name.size() + 1, name + "<") != 0) continue;
                if (std::find(made.begin(), made.end(), candidateSpecializations_[k]) == made.end())
                    made.push_back(candidateSpecializations_[k]);
            }
        }
        if (made.empty()) {
            // None deduced: the first one's reason, as its own deduction says it.
            std::string why;
            Bound bound(this);
            if (!mt.classParams.empty())
                bindTemplateParameters(mt.classParams, mt.classBinding, mt.classValues, mt.classPacks, &bound.undo);
            deduceTemplateArguments(mt, argTypes, &binding, &values, &packs, &why);
            bound.release();
            if (why.empty()) why = "no specialization of it could be made for these arguments";
            src_.fail(pos, "'" + plain->describe() + "::" + name + "' is a "
                           "member function template and " + why);
        }
    }

    const Signature *sig = &functions_[made[0]];
    if (sig == nullptr)
        src_.fail(pos, "'" + plain->describe() + "::" + name + "' could not be "
                       "instantiated with these template arguments");

    std::vector<ExprPtr> callArgs;
    if (deduced) {
        callArgs = std::move(deducedArgs);
        const std::string key = ownKey("$explicit:" + plain->tag() + "::" + name);
        functionIndex_[key] = made;
        const Signature chosen = object != nullptr
            ? resolveOverload(key, callArgs, pos, object->type(), objectIsLvalue(*object))
            : resolveOverload(key, callArgs, pos);
        functionIndex_.erase(key);
        sig = &functions_[made[0]];
        for (std::size_t i : made)
            if (functions_[i].symbol == chosen.symbol) sig = &functions_[i];
    } else {
        // `&destroyOne<T>`: a static member template's specialization named without a call is a function, its
        // address an ordinary pointer ([expr.unary.op]/3; 1.82: "naming one without calling it is not supported").
        if (!peek().is("(") && made.size() == 1 && functions_[made[0]].isStaticMember) {
            Signature &f = functions_[made[0]];
            markUsed(&f);
            for (Signature &g : functions_)
                if (g.symbol == f.symbol) g.used = true;
            Var *v = Var::global(f.name);
            v->setSymbol(f.symbol);
            ExprPtr designator(v);
            designator->setType(types_.functionType(f.returns, f.params, f.variadic));
            return designator;
        }
        if (!peek().is("("))
            src_.fail(peek().pos, "'" + name + "' is a member function "
                                  "template, and naming one without calling it "
                                  "is not supported yet");
        at_++;
        parseArguments(callArgs);
        sig = &functions_[made[0]];        // the arguments may have grown the table
        if (made.size() > 1) {
            const std::string key = "$explicit:" + plain->tag() + "::" + name;
            functionIndex_[key] = made;
            const Signature chosen = object != nullptr
                ? resolveOverload(key, callArgs, pos, object->type(), objectIsLvalue(*object))
                : resolveOverload(key, callArgs, pos);
            functionIndex_.erase(key);
            for (std::size_t i : made)
                if (functions_[i].symbol == chosen.symbol) sig = &functions_[i];
        }
    }

    // **The specialization is odr-used here**, so its definition stays: a
    // call with explicit arguments never passes through resolveOverload,
    // and `s.m<int>(1)` was pruned to a link error.
    markUsed(sig);
    // A copy, since applyDefaults and completeCall read it while the arguments
    // can grow functions_ out from under a reference.
    const Signature chosen = *sig;
    applyDefaults(chosen, callArgs, pos);

    // A static member template takes no `this`; an object it was named through is still evaluated.
    if (chosen.isStaticMember) {
        ExprPtr call = completeCall(name, chosen.symbol, nullptr, chosen.returns, chosen.params,
                                    chosen.variadic, pos, std::move(callArgs), false);
        if (object == nullptr || clonePure(*object) != nullptr) return call;
        const Type *rt = call->type();
        ExprPtr both(new Comma(std::move(object), std::move(call)));
        both->setType(rt);
        return both;
    }
    if (object == nullptr)
        src_.fail(pos, "'" + plain->describe() + "::" + name + "' is a member function "
                       "template that is not static, so it needs an object to be called on");

    if (obj->isConst() && !chosen.constThis)
        src_.fail(pos, "'" + name + "' is not a const member function, and this "
                       "object is const");
    if (!refQualifierViable(chosen.refThis, chosen.constThis, objectIsLvalue(*object)))
        src_.fail(pos, refQualifierRefusal(name, objectIsLvalue(*object)));

    if (obj->isVolatile() && !chosen.volatileThis)
        src_.fail(pos, "'" + name + "' is not a volatile member function, and this object is volatile");
    const Type *pointee = chosen.constThis ? types_.withConst(plain) : plain;
    if (chosen.volatileThis) pointee = types_.withVolatile(pointee);
    const Type *thisType = types_.pointerTo(pointee);
    // `this` is the declaring base's address, which convert() moves to where that base sits.
    ExprPtr addr(new Unary('&', std::move(object)));
    addr->setType(types_.pointerTo(types_.withCvOf(named, obj)));
    if (plain != named) addr = convert(std::move(addr), thisType);
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
    // head<3>; a constructor's is spelled from the class's name, CMatrix<3>, and keyed apart from it.
    const std::string display =
        specializationKey(mt.isConstructor ? mt.ownerLocal : mt.name, args);
    const std::string key = mt.ownerTag + "::" + display;
    // Two templates of one name may each be instantiated at K = 3: the cache is per template.
    const std::string made = key + "@" + std::to_string(mt.afterParams);
    if (memberTemplateMade_.count(made) != 0) return &functions_[memberTemplateMade_[made]];
    // **A specialization asking for itself** - `left.sortBy(comp)` in sortBy<C>'s body - is the one its own
    // replay declared before the body began; replaying it again would define it twice.
    const std::map<std::string, std::size_t>::const_iterator busy = memberTemplateBusy_.find(made);
    if (busy != memberTemplateBusy_.end()) {
        if (const std::vector<std::size_t> *had = overloadsOf(key))
            for (std::size_t i = 0; i < had->size(); i++)
                if ((*had)[i] >= busy->second) {
                    functions_[(*had)[i]].fromMemberTemplate = true;
                    if (mt.isConstructor && tokens_[mt.afterParams].is("explicit")) functions_[(*had)[i]].isExplicit = true;
                    return &functions_[(*had)[i]];
                }
        return nullptr;
    }
    const std::size_t before = functions_.size();
    struct Busy {
        Parser *p; std::string made;
        ~Busy() { p->memberTemplateBusy_.erase(made); }
    } busyNow{ this, made };
    memberTemplateBusy_[made] = before;
    (void)pos;  // only declared (sizeof idiom): a declaration is enough

    // Both layers come off however the replay ends: a body that fails throws past here, and the caller -
    // a member of the class one level out, `Tu<P, int>` asking `Tu<int>` - read its own H as `int` after.
    struct Unbind {
        Parser *p;
        std::vector<Shadow> undo;
        ~Unbind() { p->unbindTemplateParameters(undo); }
    } guard{ this, std::vector<Shadow>() };
    std::vector<Shadow> &undo = guard.undo;
    // The class's parameters first (N), then the member's own (M) - two layers,
    // the whole reason a member template records the class's binding.
    if (!mt.classParams.empty())
        bindTemplateParameters(mt.classParams, mt.classBinding, mt.classValues,
                               mt.classPacks, &undo);
    // A member template's own pack is carried in its argument list.
    Packs packs(mt.params.size());
    for (std::size_t i = 0; i < args.size() && i < packs.size(); i++)
        if (args[i].isPack) { packs[i] = args[i].pack; packs[i].values = args[i].packValues; }
    bindTemplateParameters(mt.params, binding, values, packs, &undo);

    // **The pattern the Itanium name is spelled from**: the member's own parameters as
    // references to themselves, the class's already bound to its arguments above.
    std::vector<const Type *> pattern(mt.params.size());
    for (std::size_t i = 0; i < mt.params.size(); i++)
        pattern[i] = types_.templateParam(static_cast<int>(i));
    std::string patternName;
    const Type *patternFn = readTemplateDeclaration(mt, pattern, values, &patternName);

    const bool wasInst = memberTemplateInst_;
    const std::string wasOf = memberTemplateOf_;
    const std::string wasName = memberTemplateName_;
    const std::vector<TemplateArg> wasArgs = memberTemplateArgs_;
    const Type *wasPattern = memberTemplatePattern_;
    memberTemplateInst_ = true;
    memberTemplateOf_ = mt.name;
    memberTemplateName_ = display;
    memberTemplateArgs_ = target_.microsoftNames() ? args : declaredArguments(mt.params, args);
    memberTemplateAccess_ = mt.memberAccess;
    memberTemplatePattern_ = patternFn;

    std::vector<PendingBody> one(1);
    one[0].tag = mt.ownerTag;
    one[0].start = mt.afterParams;
    one[0].local = mt.ownerLocal.empty() ? mt.ownerTag : mt.ownerLocal;
    one[0].key = key;
    one[0].which = PendingBody::npos();
    struct Restore {
        Parser *p; bool inst; std::string of, name; std::vector<TemplateArg> args; const Type *pattern;
        ~Restore() {
            p->memberTemplateInst_ = inst; p->memberTemplateOf_ = of; p->memberTemplateName_ = name;
            p->memberTemplateArgs_ = args; p->memberTemplatePattern_ = pattern;
        }
    } restore{ this, wasInst, wasOf, wasName, wasArgs, wasPattern };
    // A default argument is read where the call is (item 40, `const C &c = C()`): it keeps both layers' binding.
    struct Layers {
        Parser *p; std::vector<TemplateParam> params; std::vector<const Type *> binding; std::vector<long long> values;
        ~Layers() { p->instantiatingParams_ = params; p->instantiatingBinding_ = binding; p->instantiatingValues_ = values; }
    } layers{ this, instantiatingParams_, instantiatingBinding_, instantiatingValues_ };
    {
        std::vector<TemplateParam> all = mt.classParams;
        std::vector<const Type *> bound = mt.classBinding;
        std::vector<long long> vals = mt.classValues;
        bound.resize(all.size());
        vals.resize(all.size());
        all.insert(all.end(), mt.params.begin(), mt.params.end());
        bound.insert(bound.end(), binding.begin(), binding.end());
        vals.insert(vals.end(), values.begin(), values.end());
        instantiatingParams_ = all;
        instantiatingBinding_ = bound;
        instantiatingValues_ = vals;
    }
    // Its body, mem-initialisers and all, names its own namespace's things wherever it is asked for from:
    // `__cxx1_head(allocator_arg, a, ...)` in std::tuple, made for a call in main.
    struct Where {
        Parser *p; std::vector<std::string> was;
        ~Where() { p->namespaceStack_ = was; }
    } where{ this, namespaceStack_ };
    // A global one's too: read in the namespace a call came from, `twice` in a global class's member template
    // asked for inside `ad` was `ad::twice` (1.82).
    namespaceStack_ = namespacePathStack(mt.ns);
    // **Read once, and perhaps inside a trial or an unevaluated operand**, whose marks of use are put back when
    // it ends: what this body called is kept beside the specialization, and asked for again if the program
    // turns out to use it (carryTrialUses). Only this - what the operand itself named stays unused (1.77).
    std::vector<char> usedBefore;
    if (usesHeld_ > 0) {
        usedBefore.resize(functions_.size());
        for (std::size_t i = 0; i < usedBefore.size(); i++) usedBefore[i] = functions_[i].used ? 1 : 0;
    }
    replayInlineBodies(one);
    unbindTemplateParameters(undo);
    undo.clear();
    TrialUses calledByBody;
    if (usesHeld_ > 0)
        for (std::size_t i = 0; i < functions_.size(); i++)
            if (functions_[i].used && (i >= usedBefore.size() || !usedBefore[i])) calledByBody.called.push_back(i);

    // The function this replay declared under the key - the first one past `before`.
    const std::vector<std::size_t> *had = overloadsOf(key);
    if (had == nullptr) return nullptr;
    for (std::size_t i = 0; i < had->size(); i++)
        if ((*had)[i] >= before) {
            memberTemplateMade_[made] = (*had)[i];
            functions_[(*had)[i]].fromMemberTemplate = true;
            if (!calledByBody.called.empty()) {
                calledByBody.made.push_back((*had)[i]);
                trialUses_.push_back(calledByBody);
            }
            // [class.conv.ctor]: an `explicit` constructor template makes explicit constructors (item 29).
            if (mt.isConstructor && tokens_[mt.afterParams].is("explicit")) functions_[(*had)[i]].isExplicit = true;
            return &functions_[(*had)[i]];
        }
    return &functions_[(*had)[0]];
}

// The arguments a function template's Itanium name spells, each value of a parameter whose declared type
// depends on another parameter carrying that type for `Tn` (clang 17's <template-param-decl>).
std::vector<TemplateArg> Parser::declaredArguments(const std::vector<TemplateParam> &params,
                                                   const std::vector<TemplateArg> &args) const {
    std::vector<TemplateArg> out = args;
    for (std::size_t i = 0; i < out.size() && i < params.size(); i++)
        if (params[i].type != nullptr && !params[i].isPack && (params[i].typeOf >= 0 || params[i].builtOnParam))
            out[i].declaredAs = params[i].type;
    return out;
}

// **Function templates as overload-resolution candidates.**
void Parser::instantiateViableTemplates(const std::string &name,
                                        const std::vector<const Type *> &argTypes,
                                        std::size_t pos) {
    std::map<std::string, std::vector<TemplateDecl> >::const_iterator it =
        fnTemplates_.find(name);
    if (it == fnTemplates_.end()) return;
    const std::vector<TemplateDecl> overloads = it->second;
    instantiateViableTemplates(overloads, argTypes, pos);
}

std::vector<Parser::TemplateDecl> Parser::templatesNamed(const std::string &name, const std::string &scope,
                                                         std::string *ns) const {
    const std::string::size_type cut = name.rfind("::");
    const std::string q = cut == std::string::npos ? std::string() : name.substr(0, cut + 2);
    const std::string bare = cut == std::string::npos ? name : name.substr(cut + 2);
    ns->clear();
    std::map<std::string, std::vector<TemplateDecl> >::const_iterator it = fnTemplates_.find(bare);
    if (it == fnTemplates_.end()) return std::vector<TemplateDecl>();
    std::string here = scope.empty() ? namespacePrefix() : scope + "::";
    // [namespace.qual]: `user::f` looks in user and the inline namespaces in it, never in another namespace.
    if (!scope.empty()) {
        std::vector<TemplateDecl> got;
        for (const TemplateDecl &d : it->second) {
            bool inScope = d.ns == here;
            if (!inScope && d.ns.size() > here.size() && d.ns.compare(0, here.size(), here) == 0) {
                inScope = true;
                std::string path = scope;
                for (std::size_t s = here.size(), e; inScope && s < d.ns.size(); s = e + 2) {
                    e = d.ns.find("::", s);
                    if (e == std::string::npos) { inScope = false; break; }
                    path += "::" + d.ns.substr(s, e - s);
                    inScope = inlineNamespaces_.count(path) != 0;
                }
            }
            if (inScope) got.push_back(d);
        }
        if (!got.empty()) *ns = here;
        return got;
    }
    for (;;) {
        std::vector<TemplateDecl> got;
        for (const TemplateDecl &d : it->second)
            if (d.ns == here + q) got.push_back(d);
        if (!got.empty()) { *ns = here + q == "" ? "::" : here + q; return got; }
        if (here.empty()) break;
        const std::string::size_type up = here.rfind("::", here.size() - 3);
        here = up == std::string::npos ? std::string() : here.substr(0, up + 2);
    }
    return it->second;
}

void Parser::instantiateViableTemplates(const std::vector<TemplateDecl> &overloads,
                                        const std::vector<const Type *> &argTypes, std::size_t pos,
                                        std::set<std::string> *made) {
    for (std::size_t k = 0; k < overloads.size(); k++) {
        const TemplateDecl &decl = overloads[k];
        std::vector<const Type *> binding;
        std::vector<long long> values;
        Packs packs;
        std::string why;
        if (!deduceTemplateArguments(decl, argTypes, &binding, &values, &packs,
                                     &why))
            continue;
        const std::vector<TemplateArg> args = argumentsFromBinding(decl.params, binding, values, packs);
        try {
            Trial trial(this);
            const std::string symbol = instantiate(decl, binding, values, args, pos, packs).symbol;
            if (made != nullptr) made->insert(symbol);
        } catch (const SubstitutionFailure &f) {
            // others may apply; a missing feature is no [temp.deduct]/8 drop
            if (f.unsupported) src_.fail(f.pos, f.why);
        }
    }
}

// **A static member reached through a class template-id**, `CNeeds<(N==3)>::
// check()` or `std::numeric_limits<int>::max()`.
ExprPtr Parser::templateIdMember(const Type *cls, std::size_t pos) {
    (void)pos;
    // `O<T>::template get<int>()` - and [temp.names]/4: where the qualifier depends on a template parameter,
    // a member template named with arguments is not one without the keyword.
    const bool dependentQualifier = at_ >= 2 && tokens_[at_ - 1].is("::") &&
                                    (tokens_[at_ - 2].is(">") || tokens_[at_ - 2].is(">>")) &&
                                    qualifierDependsOnParameter(at_ - 1);
    const bool keyword = consume("template");
    std::size_t mpos = peek().pos;
    std::string member = declaredName("a member name");
    if (dependentQualifier && !keyword && peek().is("<") && memberTemplateOwner(cls, member) != nullptr)
        src_.fail(mpos, "'" + member + "' is a member template of a class that depends on a template parameter, "
                        "and naming it with arguments there needs 'template' in front - [temp.names]/4");
    // `Tree<int>::It::Deep::twice(2)`: a nested class is a scope of its own, walked a name at a time.
    while (peek().is("::") && peekAt(1).kind == TokenKind::Ident) {
        const Type *nested = realizeMemberClass(lookupInClass(cls, member));
        if (nested == nullptr || !nested->isStructOrUnion()) break;
        at_++;
        cls = nested;
        mpos = peek().pos;
        member = declaredName("a member name");
    }
    // `Q<int>::W<double>::v` and `Q<int>::W<double>(1)`: a member class template's specialization, [temp.mem]/1.
    if (peek().is("<") && memberTemplateOwner(cls, member) == nullptr)
        if (const TemplateDecl *mct = memberClassTemplateOf(realizeMemberClass(cls, true), member)) {
            const TemplateDecl decl = *mct;
            if (dependentQualifier && !keyword)
                src_.fail(mpos, "'" + member + "' is a member template of a class that depends on a template "
                                "parameter, and naming it with arguments there needs 'template' in front - "
                                "[temp.names]/4");
            checkMemberTemplateAccess(decl, mpos);
            const Type *inner = instantiateClass(decl, mpos);
            if (peek().is("::") && (peekAt(1).kind == TokenKind::Ident || peekAt(1).is("template"))) {
                at_++;
                return templateIdMember(inner, pos);
            }
            if (peek().is("(")) { at_++; return classTemporary(inner, mpos); }
            src_.fail(peek().pos, "'" + inner->describe() + "' is a type, and an expression was wanted here");
        }
    // [class.qual]/2: `Tree::Tree` and `Tree<int>::Tree` name the constructor, not a type.
    if (cls->isSpecialization() && member == cls->templateName())
        src_.fail(mpos, "'" + cls->describe() + "::" + member + "' names the constructor, "
                        "not a type - write '" + member + "(...)'");
    const std::string key = cls->tag() + "::" + member;
    // `W<int>::size('x')`: a static member template, its specialization made as a call through an object makes one.
    if ((peek().is("(") || peek().is("<")) && memberTemplateOwner(cls, member) != nullptr) {
        bool ordinary = false;
        if (const std::vector<std::size_t> *set = overloadsOf(key))
            for (std::size_t i : *set) ordinary = ordinary || !functions_[i].fromTemplate;
        if (!ordinary) return memberTemplateCall(nullptr, cls, member, mpos);
    }
    // **`C<int>::T(3)` and `C<int>::Nested(1)`** - a member type, cast to or built as a temporary.
    if (peek().is("(") && overloadsOf(key) == nullptr)
        if (const Type *named = lookupInClass(cls, member)) {
            if (named->isStructOrUnion()) { at_++; return classTemporary(named, mpos); }
            return functionalCast(named, mpos);
        }
    // **`X<void>::f()` where `f` is a data member** - a pointer to function, or an object with an
    // `operator()`: the member is the postfix-expression of [expr.call]/1 and the call is made of its
    // value, so the member is read as one not called is, and the `(` is left to the caller (1.77).
    const bool dataMemberCalled = peek().is("(") && overloadsOf(key) == nullptr &&
                                  (cls->findStaticMember(member) != nullptr || cls->findMember(member) != nullptr);
    if (peek().is("(") && !dataMemberCalled) {
        at_++;
        std::vector<ExprPtr> callArgs;
        parseArguments(callArgs);
        // **`CVector<I>::operator*(s)` in a member of CVector<4> or of a class derived from it** is
        // `Base::f(...)`: called on `this`, never dispatched, [expr.call]/1 - as for a plain base name.
        const Local *held = findLocal("this");
        if (held != nullptr && !inStaticMember_ && currentClass_ != nullptr && overloadsOf(key) != nullptr &&
            (currentClass_->unqualified() == cls->unqualified() ||
             publicBaseOffset(currentClass_, cls->unqualified()) >= 0)) {
            bool anyMember = false;
            for (std::size_t i : *overloadsOf(key)) anyMember = anyMember || needsThis(functions_[i]);
            if (anyMember) {
                ExprPtr self(Var::local("this", held->offset));
                self->setType(held->type);
                ExprPtr obj(new Unary('*', std::move(self)));
                obj->setType(held->type->pointee());
                return memberCallWith(std::move(obj), held->type->pointee(), member, mpos,
                                      std::move(callArgs), cls);
            }
        }
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
        n->setType(e->type != nullptr ? e->type : types_.intType());
        return n;
    }
    // `Base<T>::v = x` inside a member of a class derived from the specialization: `(*this).Base<T>::v`,
    // [class.mfct.non-static]/3 (1.27, 2026-10-21, review of 1.25, item 3).
    if (const Member *m = cls->findMember(member))
        if (ExprPtr mine = qualifiedMemberThroughThis(cls, *m, member, mpos)) return mine;
    if (unevaluated_ > 0)
        if (const Member *m = cls->findMember(member)) return memberWithoutObject(cls, *m, mpos);
    src_.fail(mpos, "'" + cls->describe() + "' has no static member or "
                    "enumerator called '" + member + "'");
}

// **A member function template as an overload-resolution candidate.**
void Parser::instantiateViableMemberTemplates(
    const Type *cls, const std::string &name,
    const std::vector<const Type *> &argTypes, std::size_t pos) {
    if (cls == nullptr || cls->tag().empty()) return;
    std::map<std::string, std::vector<TemplateDecl> >::const_iterator it =
        memberTemplates_.find(cls->tag() + "::" + name);
    if (it == memberTemplates_.end()) return;
    const std::vector<TemplateDecl> all = it->second;
    for (std::size_t t = 0; t < all.size(); t++) instantiateViableMemberTemplate(all[t], argTypes, pos);
}

void Parser::instantiateViableMemberTemplatesUnder(
    const std::string &key, const std::vector<const Type *> &argTypes, std::size_t pos) {
    std::map<std::string, std::vector<TemplateDecl> >::const_iterator it =
        memberTemplates_.find(key);
    if (it != memberTemplates_.end()) {
        const std::vector<TemplateDecl> all = it->second;
        for (std::size_t t = 0; t < all.size(); t++) instantiateViableMemberTemplate(all[t], argTypes, pos);
    }
    // [class.inhctor]: what `using B::B` inherits of a constructor template is its specializations.
    for (const auto &inherits : inheritFrom_) {
        if (constructorKey(inherits.first) != key) continue;
        const std::string tag = inherits.first;
        const std::vector<const Type *> bases = inherits.second;
        for (const Type *base : bases)
            if (memberTemplates_.count(constructorKey(base->tag())) != 0) {
                instantiateViableMemberTemplatesUnder(constructorKey(base->tag()), argTypes, pos);
                declareInheritingCtors(tag, findTypedef(tag), pos);
            }
        break;
    }
}

void Parser::instantiateConstructorTemplates(const Type *cls,
                                             const std::vector<const Type *> &argTypes,
                                             std::size_t pos) {
    if (cls == nullptr || !cls->isStructOrUnion() || cls->tag().empty()) return;
    instantiateViableMemberTemplatesUnder(constructorKey(cls->unqualified()->tag()), argTypes, pos);
}

void Parser::instantiateConstructorTemplates(const Type *cls, const std::vector<ExprPtr> &args,
                                             std::size_t pos) {
    std::vector<const Type *> argTypes;
    for (std::size_t i = 0; i < args.size(); i++) argTypes.push_back(deductionArgType(*args[i]));
    instantiateConstructorTemplates(cls, argTypes, pos);
}

// One member template as a candidate: deduced, and instantiated where that works.
void Parser::instantiateViableMemberTemplate(const TemplateDecl &mt,
                                             const std::vector<const Type *> &argTypes,
                                             std::size_t pos) {

    std::vector<const Type *> binding;
    std::vector<long long> values;
    Packs packs;
    std::string why;
    Bound bound(this);
    std::vector<Shadow> &undo = bound.undo;
    if (!mt.classParams.empty())
        bindTemplateParameters(mt.classParams, mt.classBinding, mt.classValues,
                               mt.classPacks, &undo);
    const bool ok = deduceTemplateArguments(mt, argTypes, &binding, &values,
                                            &packs, &why);
    bound.release();
    if (!ok) return;

    const std::vector<TemplateArg> args = argumentsFromBinding(mt.params, binding, values, packs);
    const bool wasBody = memberTemplateBody_;
    memberTemplateBody_ = false;
    const std::size_t before = functions_.size();
    try {
        Trial trial(this);
        const Signature *made = instantiateMemberTemplate(mt, binding, values, args, pos);
        if (made != nullptr)
            candidateSpecializations_.push_back(static_cast<std::size_t>(made - &functions_[0]));
    } catch (const SubstitutionFailure &f) {
        // Not this one - unless a refusal, a fact about the compiler. A failed
        // body is no substitution failure, [temp.deduct]/8, but only the
        // chosen candidate's body is instantiated ([temp.inst]/3) and errors.
        if (f.unsupported) src_.fail(f.pos, f.why);
        if (memberTemplateBody_) {
            const std::string said = f.why + " - in the instantiation of '" + mt.ownerTag + "::" +
                                     specializationKey(mt.isConstructor ? mt.ownerLocal : mt.name, args) +
                                     "', asked for at " + src_.where(pos);
            bool kept = false;
            for (std::size_t i = before; i < functions_.size(); i++)
                if (functions_[i].owner == mt.ownerTag) {
                    failedBodies_[functions_[i].symbol] = std::make_pair(f.pos, said);
                    candidateSpecializations_.push_back(i);
                    // What instantiateMemberTemplate marks once the body is read, marked here without it.
                    functions_[i].fromMemberTemplate = true;
                    if (mt.isConstructor && tokens_[mt.afterParams].is("explicit")) functions_[i].isExplicit = true;
                    if (!kept)
                        memberTemplateMade_[mt.ownerTag + "::" +
                                            specializationKey(mt.isConstructor ? mt.ownerLocal : mt.name, args) +
                                            "@" + std::to_string(mt.afterParams)] = i;
                    kept = true;
                }
            if (!kept) src_.fail(f.pos, said);
        }
    }
    memberTemplateBody_ = wasBody;
}

void Parser::instantiateConversionTemplates(const Type *cls, const Type *to, std::size_t pos) {
    // [temp.deduct.conv]/1-3: A is the type required, a reference's referent and without its own cv.
    const Type *a = (to->isReference() ? to->referent() : to)->unqualified();
    for (const Type *c = cls->unqualified(); c != nullptr; c = c->base()) {
        std::map<std::string, std::vector<TemplateDecl> >::const_iterator it =
            memberTemplates_.find(c->tag() + "::" + conversionTemplateKey());
        if (it == memberTemplates_.end()) continue;
        // A copy: making one adds to the tables this was found in.
        const std::vector<TemplateDecl> conversions = it->second;
        for (std::size_t k = 0; k < conversions.size(); k++) {
            TemplateDecl mt = conversions[k];
            std::size_t at = mt.afterParams;
            while (at < tokens_.size() && !tokens_[at].is("operator") && !tokens_[at].is("(")) at++;
            if (at >= tokens_.size() || !tokens_[at].is("operator")) continue;
            const std::size_t resume = at_;
            // The conversion's own type, its parameters standing for themselves: P.
            const Type *p = nullptr;
            {
                Bound bound(this);
                std::vector<Shadow> &undo = bound.undo;
                if (!mt.classParams.empty())
                    bindTemplateParameters(mt.classParams, mt.classBinding, mt.classValues, mt.classPacks, &undo);
                std::vector<const Type *> self(mt.params.size());
                for (std::size_t i = 0; i < self.size(); i++) self[i] = types_.templateParam(static_cast<int>(i));
                bindTemplateParameters(mt.params, self, std::vector<long long>(mt.params.size(), 0), Packs(), &undo);
                at_ = at;
                operatorName();
                p = conversionTarget_;
                conversionTarget_ = nullptr;
                at_ = resume;
                bound.release();
            }
            if (p == nullptr) continue;
            // /2-3: P's referent if it is a reference; else an array or a function as the pointer it
            // decays to where A is no reference, and P's own cv dropped.
            if (p->isReference()) p = p->referent();
            else if (!to->isReference() && p->isArray()) p = types_.pointerTo(p->pointee());
            else if (!to->isReference() && p->isFunction()) p = types_.pointerTo(p);
            p = p->unqualified();
            std::vector<const Type *> binding(mt.params.size(), static_cast<const Type *>(nullptr));
            std::vector<long long> values(mt.params.size(), 0);
            std::string why;
            if (!deduceOne(p, a, &binding, &values, &why)) continue;
            bool complete = true;
            for (std::size_t i = 0; i < binding.size(); i++)
                if (binding[i] == nullptr && mt.params[i].type == nullptr) complete = false;
            if (!complete) continue;
            // Made under the name its replay declares - `operator double *` - the one a conversion is found by.
            {
                Bound bound(this);
                std::vector<Shadow> &undo = bound.undo;
                if (!mt.classParams.empty())
                    bindTemplateParameters(mt.classParams, mt.classBinding, mt.classValues, mt.classPacks, &undo);
                bindTemplateParameters(mt.params, binding, values, Packs(), &undo);
                at_ = at;
                mt.name = operatorName();
                conversionTarget_ = nullptr;
                at_ = resume;
                bound.release();
            }
            const std::vector<TemplateArg> args = argumentsFromBinding(mt.params, binding, values, Packs());
            const bool wasBody = memberTemplateBody_;
            memberTemplateBody_ = false;
            try {
                Trial trial(this);
                const Signature *made = instantiateMemberTemplate(mt, binding, values, args, pos);
                if (made != nullptr)
                    candidateSpecializations_.push_back(static_cast<std::size_t>(made - &functions_[0]));
            } catch (const SubstitutionFailure &f) {
                if (f.unsupported) src_.fail(f.pos, f.why);
                if (memberTemplateBody_)
                    src_.fail(f.pos, f.why + " - in the instantiation of '" + mt.ownerTag + "::" +
                                         mt.name + "', asked for at " + src_.where(pos));
            }
            memberTemplateBody_ = wasBody;
        }
    }
}

bool Parser::dependentArgs(const std::vector<TemplateArg> &args) const {
    for (const TemplateArg &a : args) {
        if (a.isParam || a.dependentValue || a.expr != nullptr) return true;
        if (a.isPack) {
            for (const Type *m : a.pack)
                if (dependentType(m)) return true;
        } else if (a.isType && dependentType(a.type)) {
            return true;
        }
    }
    return false;
}

bool Parser::dependentType(const Type *t) const {
    for (int depth = 0; t != nullptr && depth < 64; depth++) {
        if (t->kind() == Kind::TemplateParam || t->kind() == Kind::DependentMember ||
            t->kind() == Kind::DependentExpr)
            return true;
        if (t->unqualified() != t) { t = t->unqualified(); continue; }
        // `R T::*` and `R (T::*)(P...)`: a pointer to member depends where its type or its class does.
        if (t->isMemberPointer() || t->isMemberFunctionPointer())
            return dependentType(t->pointee()) || dependentType(t->enclosing());
        if (t->kind() == Kind::PackExpansion) return true;
        if (t->isStructOrUnion()) {
            int tt;
            return t->isSpecialization() &&
                   (ttRefName(t->templateName(), &tt) || dependentArgs(t->templateArgs()));
        }
        if (t->kind() == Kind::Function) {
            for (const Type *p : t->params())
                if (dependentType(p)) return true;
            t = t->returns();
            continue;
        }
        if (t->isArray() && t->dependentBound() != nullptr) return true;
        if (t->isPointer() || t->isReference() || t->isArray()) { t = t->pointee(); continue; }
        return false;
    }
    return false;
}

std::vector<Parser::TemplateDecl> Parser::probeTargets() const {
    std::vector<TemplateDecl> out;
    std::vector<const TemplateDecl *> all;
    for (const auto &named : fnTemplates_)
        for (const TemplateDecl &d : named.second) all.push_back(&d);
    // A member template of a class that is no template, `struct C { template <class T> int m(T); }`.
    for (const auto &named : memberTemplates_)
        for (const TemplateDecl &d : named.second)
            if (d.isMember && d.classParams.empty() && !d.isConstructor) all.push_back(&d);
    // A class template, made with the stand-ins and every member of it wanted.
    for (const auto &named : templates_) {
        const TemplateDecl &d = named.second;
        if (!d.isClass || d.isAlias || !d.ownerTag.empty() || d.afterParams == 0) continue;
        bool plain = true;
        for (const TemplateParam &p : d.params) plain = plain && !p.isPack && !p.isTemplate;
        if (plain && src_.locate(d.pos).file == 0) out.push_back(d);
    }
    for (const TemplateDecl *dp : all) {
        const TemplateDecl &d = *dp;
        {
            if (!d.defined || d.isClass || d.afterParams == 0) continue;
            if (d.isMember && !d.classParams.empty()) continue;
            if (src_.locate(d.pos).file != 0) continue;
            bool templ = false;
            for (const TemplateParam &p : d.params) templ = templ || p.isTemplate;
            if (templ) continue;
            bool made = false;
            for (const Specialization &sp : specializations_) made = made || (!sp.isClass && sp.start == d.afterParams);
            // A member template's specializations are kept by key, `S::add<int>@<where its parameters end>`.
            const std::string at = "@" + std::to_string(d.afterParams);
            for (const auto &m : memberTemplateMade_)
                made = made || (m.first.size() > at.size() && m.first.compare(m.first.size() - at.size(), at.size(), at) == 0);
            if (!made) out.push_back(d);
        }
    }
    return out;
}

void Parser::probeUninstantiated(Program &program) {
    const std::vector<TemplateDecl> targets = probeTargets();
    src_.beginTrial();
    struct End {
        const Source &s;
        ~End() { s.endTrial(); }
    } end{ src_ };
    for (const TemplateDecl &d : targets) {
        if (probeSkip_.count(d.afterParams) != 0) continue;
        std::vector<const Type *> binding(d.params.size());
        std::vector<long long> values(d.params.size(), 1);
        Packs packs(d.params.size());
        for (std::size_t i = 0; i < d.params.size(); i++)
            if (!d.params[i].isPack)
                binding[i] = d.params[i].type != nullptr || probeVariant_ == 0 ? types_.intType()
                                                                                : types_.get(Kind::Double);
        try {
            if (d.isClass) {
                // `<int, 1>` written at the end of the stream and read as a use would read it.
                const std::size_t at = tokens_.size();
                auto add = [&](TokenKind kind, const std::string &text, long long value) {
                    Token t;
                    t.kind = kind;
                    t.text = text;
                    t.value = value;
                    t.pos = d.pos;
                    tokens_.push_back(t);
                };
                add(TokenKind::Punct, "<", 0);
                for (std::size_t i = 0; i < d.params.size(); i++) {
                    if (i > 0) add(TokenKind::Punct, ",", 0);
                    if (d.params[i].type != nullptr) add(TokenKind::Num, "1", 1);
                    else add(TokenKind::Keyword, probeVariant_ == 0 ? "int" : "double", 0);
                }
                add(TokenKind::Punct, ">", 0);
                add(TokenKind::Punct, ";", 0);
                add(TokenKind::End, std::string(), 0);
                at_ = at;
                const Type *cls = instantiateClass(d, d.pos);
                if (cls == nullptr) continue;
                for (Signature &f : functions_)
                    if (f.owner == cls->tag()) f.used = true;
                for (int pass = 0; pass < 16; pass++) {
                    const std::size_t had = program.functions.size();
                    instantiatePending();
                    defineImplicitFunctions();
                    if (program.functions.size() == had) break;
                }
                continue;
            }
            const std::vector<TemplateArg> args = argumentsFromBinding(d.params, binding, values, packs);
            // A member template of an ordinary class is made as a member, its body read in the class's scope.
            std::string symbol;
            if (d.isMember) {
                const Signature *s = instantiateMemberTemplate(d, binding, values, args, d.pos);
                if (s == nullptr) continue;
                symbol = s->symbol;
            } else {
                symbol = instantiate(d, binding, values, args, d.pos, packs).symbol;
            }
            for (Signature &f : functions_)
                if (f.symbol == symbol) f.used = true;
            for (int pass = 0; pass < 16; pass++) {
                const std::size_t had = program.functions.size();
                instantiatePending();
                defineImplicitFunctions();
                if (program.functions.size() == had) break;
            }
        } catch (const SubstitutionFailure &f) {
            probeResult_.failed = true;
            probeResult_.pos = f.pos;
            probeResult_.why = f.why;
            probeResult_.templateAt = d.afterParams;
            probeResult_.report = !f.unsupported && probeFailureIsTheTemplates(d, f.pos, f.why);
            return;
        }
    }
}

bool Parser::probeFailureIsTheTemplates(const TemplateDecl &d, std::size_t pos, const std::string &why) const {
    // The body: the first `{` outside parentheses after the parameters, to the `}` matching it.
    std::size_t open = 0, close = 0;
    for (std::size_t k = d.afterParams, paren = 0; k < tokens_.size() && tokens_[k].kind != TokenKind::End; k++) {
        if (tokens_[k].is("(")) paren++;
        else if (tokens_[k].is(")") && paren > 0) paren--;
        else if (tokens_[k].is(";") && paren == 0) return false;
        else if (tokens_[k].is("{") && paren == 0) { open = k; break; }
    }
    if (open == 0) return false;
    for (std::size_t k = open, depth = 0; k < tokens_.size() && tokens_[k].kind != TokenKind::End; k++) {
        if (tokens_[k].is("{")) depth++;
        else if (tokens_[k].is("}") && --depth == 0) { close = k; break; }
    }
    if (close == 0 || pos < tokens_[open].pos || pos > tokens_[close].pos) return false;
    // [temp.res]/3: the missing `typename` is the template's whatever its arguments.
    if (why.find("write 'typename") != std::string::npos) return true;
    // What depends: the template's parameters, the function's, and what is declared from them.
    std::set<std::string> dependent;
    for (const TemplateParam &p : d.params) dependent.insert(p.name);
    for (std::size_t k = d.afterParams; k < open; k++)
        if (tokens_[k].kind == TokenKind::Ident &&
            (tokens_[k + 1].is(",") || tokens_[k + 1].is(")") || tokens_[k + 1].is("=") || tokens_[k + 1].is("[")))
            dependent.insert(tokens_[k].text);
    auto statementOf = [&](std::size_t k, std::size_t *from, std::size_t *to) {
        std::size_t a = k, b = k;
        while (a > open + 1 && !tokens_[a - 1].is(";") && !tokens_[a - 1].is("{") && !tokens_[a - 1].is("}")) a--;
        while (b < close && !tokens_[b].is(";") && !tokens_[b].is("{") && !tokens_[b].is("}")) b++;
        *from = a;
        *to = b;
    };
    for (int round = 0; round < 2; round++)
        for (std::size_t k = open + 1; k < close; ) {
            std::size_t a, b;
            statementOf(k, &a, &b);
            bool names = false;
            for (std::size_t j = a; j < b && !names; j++)
                names = tokens_[j].kind == TokenKind::Ident && dependent.count(tokens_[j].text) != 0;
            if (names)
                for (std::size_t j = a; j < b; j++)
                    if (tokens_[j].kind == TokenKind::Ident &&
                        (tokens_[j + 1].is("=") || tokens_[j + 1].is("(") || tokens_[j + 1].is("{") ||
                         tokens_[j + 1].is(";") || tokens_[j + 1].is(",") || tokens_[j + 1].is("[") ||
                         tokens_[j + 1].is(")") || tokens_[j + 1].is(":")) &&
                        !tokens_[j - 1].is(".") && !tokens_[j - 1].is("->") && !tokens_[j - 1].is("::"))
                        dependent.insert(tokens_[j].text);
            k = b + 1;
        }
    std::size_t at = open;
    for (std::size_t k = open; k <= close; k++)
        if (tokens_[k].pos <= pos) at = k;
    std::size_t a, b;
    statementOf(at, &a, &b);
    for (std::size_t j = a; j < b; j++)
        if (tokens_[j].kind == TokenKind::Ident && dependent.count(tokens_[j].text) != 0) return false;
    return true;
}
