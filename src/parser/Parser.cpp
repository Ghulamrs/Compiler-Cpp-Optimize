// The parser: the pieces the rest of it is built on. Reading tokens, looking a
// name up as a type or an enumerator, deciding what is being declared, replaying
// the bodies a class held back, and below those the scopes and symbol tables.

#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <algorithm>
#include <climits>
#include <cstring>

int alignTo(int n, int a) { return (n + a - 1) / a * a; }

// Recognised by the lexer, with no rule in this parser yet. Naming the keyword is
// the whole point: without this the word reaches expression parsing as an unknown
// identifier and the error lands on whatever follows it.
const char *notYetSupported(const std::string &word) {
    static const char *const pending[] = {
        "asm",
        "export"
    };
    for (const char *k : pending)
        if (word == k) return k;
    return nullptr;
}

// **A keyword this parser does implement, standing where it has no rule for it.**
// Each was measured in a place of its own before being moved here, because the
// two answers are different claims: one says the compiler cannot, one says not here.
const char *implementedElsewhere(const std::string &word) {
    static const char *const elsewhere[] = {
        "catch", "friend", "inline", "mutable", "namespace",
        "operator", "template", "using", "virtual"
    };
    for (const char *k : elsewhere)
        if (word == k) return k;
    return nullptr;
}

// **A null pointer constant**, which C++11 spells two ways: the integer
// literal 0, and `nullptr`. [conv.ptr]/1 - and everything that asks this
// question wants both, which is why `nullptr` needed no new call site here.
bool isNullConstant(const Expr &e) {
    if (e.type() != nullptr && e.type()->isNullPtr()) return true;
    const Num *n = dynamic_cast<const Num *>(&e);
    return n != nullptr && n->type()->isInteger() && n->value() == 0;
}

// Does this expression name an object at all - what [basic.lval] calls a glvalue?
// An lvalue and an xvalue both do and the difference is not asked here: reference
// binding wants an address, and either will give one.
bool isGlvalue(const Expr &e) {
    if (dynamic_cast<const Var *>(&e)) return true;
    if (dynamic_cast<const MemberAccess *>(&e)) return true;
    if (const Unary *u = dynamic_cast<const Unary *>(&e)) return u->op() == '*';
    // **A comma has the value category of its right operand** - [expr.comma], and
    // it is C++'s rule rather than C's. It matters because `b.count` for a static
    // member is built as one: the object is dropped, the shared object is named.
    if (const Comma *c = dynamic_cast<const Comma *>(&e))
        return isGlvalue(c->right());
    return false;
}

// An object something else may still be looking at: the question above minus what
// `static_cast<T &&>` hands over, and that subtraction is what an rvalue reference
// is for - it refuses an lvalue, so a move can empty what it is given.
bool isLvalue(const Expr &e) {
    // A class temporary or `&S::f` dereferences a slot of the frame and is marked a prvalue for it.
    return !e.isXvalue() && e.declCategory() != Expr::Category::Prvalue && isGlvalue(e);
}

// A class temporary is a dereference of its slot and carries the prvalue mark, so the mark is
// asked first; an xvalue is an rvalue; a reference cast marked Lvalue names an object.
bool objectIsLvalue(const Expr &e) {
    if (e.declCategory() == Expr::Category::Prvalue || e.isXvalue()) return false;
    return e.declCategory() == Expr::Category::Lvalue || isLvalue(e);
}

const Token &Parser::peekAt(std::size_t n) const {
    std::size_t i = at_ + n;
    return i < tokens_.size() ? tokens_[i] : tokens_.back();
}

// The index of the `)`, `]` or `}` that closes the group opening at tokens_[i]; the last token where none does.
// The scans that look for the `>` of a template-argument-list step over a group whole with it:
// `X<decltype(a < b), bool>` and `X<(n >> 1)>` close at their own `>`, whatever stands between the parentheses
// (1.80: each scan counted the angles inside, and `f(std::is_same<decltype(1 < 2), bool>::value)` was refused).
std::size_t Parser::groupCloseAt(std::size_t i) const {
    int depth = 0;
    for (; i < tokens_.size(); i++) {
        const Token &t = tokens_[i];
        if (t.kind == TokenKind::End) break;
        if (t.is("(") || t.is("[") || t.is("{")) depth++;
        else if ((t.is(")") || t.is("]") || t.is("}")) && --depth <= 0) return i;
    }
    return tokens_.size() - 1;
}

bool Parser::consume(const char *s) {
    if (!peek().is(s)) return false;
    at_++;
    return true;
}

void Parser::expect(const char *s) {
    if (!peek().is(s)) {
        // A missing ';' belongs to the statement before it: said just after its last token, as clang says it,
        // and not at the next token - a '}' a line further down, or the next statement's first word.
        const Token *before = at_ > 0 && at_ <= tokens_.size() ? &tokens_[at_ - 1] : 0;
        const bool after = std::string(s) == ";" && before && before->end > before->pos;
        src_.fail(after ? before->end : peek().pos, std::string("expected '") + s + "'");
    }
    at_++;
}

std::string Parser::expectIdent(const char *what) {
    if (peek().kind != TokenKind::Ident) {
        // A keyword standing where a name was wanted is a feature this parser has
        // not grown rather than a slip of the finger, and "expected a name" points
        // at the word without saying what is wrong. `int operator+(int);` is why.
        if (const char *pending = notYetSupported(peek().text))
            src_.fail(peek().pos, std::string("'") + pending +
                                  "' is not supported yet");
        if (const char *here = implementedElsewhere(peek().text))
            src_.fail(peek().pos, std::string("'") + here + "' is implemented, "
                                  "but it cannot be a name - " + what +
                                  " was wanted here");
        src_.fail(peek().pos, std::string("expected ") + what);
    }
    std::string name = peek().text;
    at_++;
    return name;
}

long long Parser::expectNumber(const char *what) {
    if (peek().kind != TokenKind::Num)
        src_.fail(peek().pos, std::string("expected ") + what);
    long long v = peek().value;
    at_++;
    return v;
}

// A class's own nested types, and its bases'. The one table this parser has
// for type names is flat and keyed by the *qualified* name, so the scope is
// walked here rather than kept as a stack of tables.
const Type *Parser::lookupInClass(const Type *cls, const std::string &name) const {
    for (const Type *c = cls; c != nullptr; c = c->enclosing()) {
        auto it = typedefIndex_.find(c->tag() + "::" + name);
        if (it != typedefIndex_.end()) return typedefs_[it->second].type;
        for (const Type::BaseSpec &b : c->bases())
            if (const Type *t = lookupInClass(b.type, name)) return t;
    }
    return nullptr;
}

bool Parser::insideClass(const Type *cls) const {
    for (std::size_t i = 0; i < classStack_.size(); i++)
        for (const Type *c = classStack_[i]; c != nullptr; c = c->enclosing())
            if (c == cls) return true;
    for (const Type *c = currentClass_; c != nullptr; c = c->enclosing())
        if (c == cls) return true;
    return false;
}

// **Does the declarator after `from` define a member of `cls`?**
bool Parser::definesMemberOf(const Type *cls, std::size_t from) const {
    if (cls == nullptr) return false;
    for (std::size_t k = from; ; k++) {
        const Token &t = peekAt(k);
        if (t.kind == TokenKind::End) return false;
        if (t.is("(") || t.is(";") || t.is("{") || t.is("=")) return false;
        if (t.kind != TokenKind::Ident || !peekAt(k + 1).is("::")) continue;

        // The longest run of `Ident ::` starting here that names a type.
        std::string name = t.text;
        const Type *best = nullptr;
        for (std::size_t j = k; peekAt(j).kind == TokenKind::Ident &&
                                peekAt(j + 1).is("::"); j += 2) {
            if (j != k) name += "::" + peekAt(j).text;
            if (const Type *found = findTypedef(name)) best = found;
        }
        if (best != nullptr)
            for (const Type *c = best; c != nullptr; c = c->enclosing())
                if (c == cls) return true;
    }
}

const std::string *Parser::localOwnerOf(const std::string &tag) const {
    auto it = localClassOwner_.find(tag);
    return it == localClassOwner_.end() ? nullptr : &it->second;
}

bool Parser::hasFunctionNamed(const std::string &key) const {
    return functionIndex_.find(key) != functionIndex_.end();
}

bool Parser::hasGlobalNamed(const std::string &key) const {
    return globalIndex_.find(key) != globalIndex_.end();
}

bool Parser::hasTypeNamed(const std::string &key) const {
    return typedefIndex_.find(key) != typedefIndex_.end();
}

// **Unqualified lookup inside a namespace, and it is a search and not a
// rule.**
std::string Parser::followUsingDeclaration(const std::string &key) const {
    std::string at = key;
    for (int hops = 0; hops < 16; hops++) {
        auto it = usingDeclarations_.find(at);
        if (it == usingDeclarations_.end()) break;
        at = it->second;
    }
    return at;
}

std::string Parser::qualifyForLookup(const std::string &name,
                                     bool (Parser::*exists)(const std::string &) const) const {
    // Written qualified: the first component is searched for, then a using-declaration followed.
    if (name.find("::") != std::string::npos) {
        // [basic.lookup.qual]/1: the first component is looked up unqualified, so
        // `inner::S` inside `outer` is `outer::inner::S` - the enclosing namespaces
        // innermost first, then the ones a using-directive opened.
        for (std::size_t i = namespaceStack_.size(); i-- > 0; ) {
            std::string prefix;
            for (std::size_t k = 0; k <= i; k++) prefix += namespaceStack_[k] + "::";
            if ((this->*exists)(prefix + name)) return prefix + name;
        }
        for (std::size_t i = usingNamespaces_.size(); i-- > 0; )
            if ((this->*exists)(usingNamespaces_[i] + "::" + name))
                return usingNamespaces_[i] + "::" + name;
        const std::string aliased = followUsingDeclaration(name);
        return aliased != name && (this->*exists)(aliased) ? aliased : name;
    }
    for (std::size_t i = namespaceStack_.size(); i-- > 0; ) {
        std::string prefix;
        for (std::size_t k = 0; k <= i; k++) prefix += namespaceStack_[k] + "::";
        const std::string key = followUsingDeclaration(prefix + name);
        if ((this->*exists)(key)) return key;
    }
    for (std::size_t i = usingNamespaces_.size(); i-- > 0; ) {
        const std::string key =
            followUsingDeclaration(usingNamespaces_[i] + "::" + name);
        if ((this->*exists)(key)) return key;
    }
    // **Last, the name as written**, which is where a using-declaration at file
    // scope is found: it declares an unqualified name and there is no prefix to
    // put in front of it.
    const std::string aliased = followUsingDeclaration(name);
    if (aliased != name && (this->*exists)(aliased)) return aliased;
    return name;
}

std::string Parser::enclosingNamespaceKey(const std::string &name,
                                          bool (Parser::*exists)(const std::string &) const) const {
    if (name.find("::") != std::string::npos) return std::string();
    for (std::size_t i = namespaceStack_.size(); i-- > 0; ) {
        std::string prefix;
        for (std::size_t k = 0; k <= i; k++) prefix += namespaceStack_[k] + "::";
        const std::string key = followUsingDeclaration(prefix + name);
        if ((this->*exists)(key)) return key;
    }
    return std::string();
}

std::string Parser::namespaceNamed(const std::string &first) const {
    // Innermost out, and a class of the name hides a namespace as a nearer namespace does:
    // inside `outer`, `inner::k` is `outer::inner`'s whichever kind it is.
    for (std::size_t i = classStack_.size(); i-- > 0; )
        if (lookupInClass(classStack_[i], first) != nullptr) return std::string();
    if (currentClass_ != nullptr && lookupInClass(currentClass_, first) != nullptr)
        return std::string();
    for (std::size_t i = namespaceStack_.size(); i-- > 0; ) {
        std::string prefix;
        for (std::size_t k = 0; k <= i; k++) prefix += namespaceStack_[k] + "::";
        if (hasTypeNamed(prefix + first)) return std::string();
        if (namespaces_.count(prefix + first)) return prefix + first;
    }
    for (std::size_t i = usingNamespaces_.size(); i-- > 0; )
        if (namespaces_.count(usingNamespaces_[i] + "::" + first))
            return usingNamespaces_[i] + "::" + first;
    return namespaces_.count(first) ? first : std::string();
}

std::size_t Parser::namespaceQualifierTokens(std::size_t from) const {
    if (peekAt(from).kind != TokenKind::Ident || !peekAt(from + 1).is("::")) return 0;
    std::string scope = namespaceNamed(peekAt(from).text);
    if (scope.empty()) return 0;
    std::size_t n = 2;
    while (peekAt(from + n).kind == TokenKind::Ident && peekAt(from + n + 1).is("::") &&
           namespaces_.count(scope + "::" + peekAt(from + n).text)) {
        scope += "::" + peekAt(from + n).text;
        n += 2;
    }
    return n;
}

std::vector<std::string> Parser::classNamespacePath(const Type *t) const {
    const Type *o = t->unqualified();
    while (o->enclosing() != nullptr) o = o->enclosing()->unqualified();
    std::string ns = o->templateNamespace();
    if (ns.empty() && o->inNamespace()) {
        const std::string &tag = o->tag();
        int depth = 0;
        for (std::size_t i = 0; i + 1 < tag.size(); i++) {
            if (tag[i] == '<') depth++;
            else if (tag[i] == '>') depth--;
            else if (depth == 0 && tag[i] == ':' && tag[i + 1] == ':') ns = tag.substr(0, i);
        }
    }
    std::vector<std::string> path;
    int depth = 0;
    std::string part;
    for (std::size_t i = 0; i < ns.size(); i++) {
        if (ns[i] == '<') depth++;
        else if (ns[i] == '>') depth--;
        if (depth == 0 && ns[i] == ':' && i + 1 < ns.size() && ns[i + 1] == ':') {
            if (!part.empty()) path.push_back(part);
            part.clear();
            i++;
            continue;
        }
        part += ns[i];
    }
    if (!part.empty()) path.push_back(part);
    return path;
}

// **An inline namespace's members are its enclosing namespace's for every lookup**, so each
// is entered under both keys: the enclosing one is `prefix` with the inline component gone.
std::vector<std::string> Parser::inlineAliasPrefixes(const std::string &prefix) const {
    std::vector<std::string> out;
    if (inlineNamespaces_.empty() || prefix.empty()) return out;
    std::vector<std::string> parts;
    std::vector<bool> isInline;
    std::string path;
    for (std::size_t s = 0, e; s < prefix.size(); s = e + 2) {
        e = prefix.find("::", s);
        if (e == std::string::npos) e = prefix.size();
        parts.push_back(prefix.substr(s, e - s));
        path += (path.empty() ? "" : "::") + parts.back();
        isInline.push_back(inlineNamespaces_.count(path) != 0);
    }
    // Every subset of the inline components deleted, the empty subset left out.
    std::vector<std::size_t> which;
    for (std::size_t i = 0; i < parts.size(); i++) if (isInline[i]) which.push_back(i);
    for (std::size_t mask = 1; mask < (std::size_t(1) << which.size()); mask++) {
        std::string alt;
        for (std::size_t i = 0, w = 0; i < parts.size(); i++) {
            if (w < which.size() && which[w] == i) { w++; if (mask & (std::size_t(1) << (w - 1))) continue; }
            alt += parts[i] + "::";
        }
        out.push_back(alt);
    }
    return out;
}

void Parser::aliasTypeKey(const std::string &key, std::size_t index) {
    const std::vector<std::string> alts = inlineAliasPrefixes(namespacePrefix());
    const std::string &prefix = namespacePrefix();
    if (alts.empty() || key.compare(0, prefix.size(), prefix) != 0) return;
    for (std::size_t a = 0; a < alts.size(); a++)
        typedefIndex_.insert(std::make_pair(alts[a] + key.substr(prefix.size()), index));
}

void Parser::aliasFunctionKey(const std::string &key, std::size_t index) {
    const std::vector<std::string> alts = inlineAliasPrefixes(namespacePrefix());
    const std::string &prefix = namespacePrefix();
    if (alts.empty() || key.compare(0, prefix.size(), prefix) != 0) return;
    for (std::size_t a = 0; a < alts.size(); a++) {
        std::vector<std::size_t> &set = functionIndex_[alts[a] + key.substr(prefix.size())];
        if (std::find(set.begin(), set.end(), index) == set.end()) set.push_back(index);
    }
}

// After each declaration in a namespace with an inline component above or at it: what it added
// to the four tables under this namespace's prefix is entered under the enclosing ones as well.
// A key already taken by another entity is left to it - the ambiguity is refused at the use.
void Parser::aliasInlineMembers(const TableSizes &before) {
    const std::vector<std::string> alts = inlineAliasPrefixes(namespacePrefix());
    if (alts.empty()) return;
    const std::string prefix = namespacePrefix();
    for (std::size_t i = before.functions; i < functions_.size(); i++) {
        const Signature &f = functions_[i];
        if (!f.owner.empty() || f.fromTemplate || f.cLinkage) continue;
        aliasFunctionKey(f.name, i);
    }
    for (std::size_t i = before.globals; i < globals_.size(); i++) {
        const std::string &key = globals_[i].name;
        if (key.compare(0, prefix.size(), prefix) != 0) continue;
        for (std::size_t a = 0; a < alts.size(); a++)
            globalIndex_.insert(std::make_pair(alts[a] + key.substr(prefix.size()), i));
    }
    for (std::size_t i = before.typedefs; i < typedefs_.size(); i++) aliasTypeKey(typedefs_[i].name, i);
    for (std::size_t i = before.enums; i < enums_.size(); i++) {
        const std::string &key = enums_[i].name;
        if (key.compare(0, prefix.size(), prefix) != 0) continue;
        for (std::size_t a = 0; a < alts.size(); a++)
            enumIndex_.insert(std::make_pair(alts[a] + key.substr(prefix.size()), i));
    }
}

void Parser::refuseInlineAmbiguity(const std::string &key, std::size_t index,
                                   const std::unordered_map<std::string, std::size_t> &table,
                                   bool (Parser::*same)(std::size_t, std::size_t) const) const {
    if (inlineNamespaces_.empty()) return;
    int depth = 0;
    std::size_t cut = std::string::npos;
    for (std::size_t i = 0; i + 1 < key.size(); i++) {
        if (key[i] == '<') depth++;
        else if (key[i] == '>') depth--;
        else if (depth == 0 && key[i] == ':' && key[i + 1] == ':') cut = i;
    }
    if (cut == std::string::npos) return;
    const std::string scope = key.substr(0, cut), name = key.substr(cut + 2);
    // The namespace's own entry and every inline descendant's, as distinct entities.
    for (std::set<std::string>::const_iterator it = inlineNamespaces_.lower_bound(scope + "::");
         it != inlineNamespaces_.end() && it->compare(0, scope.size() + 2, scope + "::") == 0; ++it) {
        std::unordered_map<std::string, std::size_t>::const_iterator at = table.find(*it + "::" + name);
        if (at == table.end() || at->second == index) continue;
        if (same != nullptr && (this->*same)(index, at->second)) continue;
        // At the name, which is the token here or the one a qualified read just took.
        const Token &named = peek().kind == TokenKind::Ident || at_ == 0 ? peek() : tokens_[at_ - 1];
        src_.fail(named.pos, "reference to '" + name + "' is ambiguous - it is declared in '" +
                             scope + "' and in its inline namespace '" + *it + "'");
    }
}

const Type *Parser::findGlobalTypedef(const std::string &name) const {
    auto it = typedefIndex_.find(name);
    return it == typedefIndex_.end() ? nullptr : typedefs_[it->second].type;
}

const Type *Parser::findTypedef(const std::string &name) const {
    // A bound template parameter's scope is inside every class and namespace around it ([temp.local]/6), and
    // no local name may redeclare it - so it comes before the function's own: `function<void()>`'s `R`, made
    // inside a function with a typedef `R`, is still void.
    if (boundTypeParams_.count(name) != 0) {
        auto it = typedefIndex_.find(name);
        if (it != typedefIndex_.end()) return typedefs_[it->second].type;
    }

    // **A class local to this function is found first, and that is what makes it
    // shadow a global of the same name** - [basic.scope.local], and the reason the
    // local scope is a table of its own rather than an entry in the global one.
    auto local = localTypes_.find(name);
    if (local != localTypes_.end()) return local->second;
    // Unqualified, the classes this is inside come next, then the enclosing namespaces,
    // and only then a global: [basic.lookup.unqual]/1, the nearest declaration wins.
    if (name.find("::") == std::string::npos)
        if (const Type *t = classScopeType(name)) return t;
    // The entry a key names, refused where an inline namespace makes it two.
    auto typeAt = [&](const std::string &key) {
        auto q = typedefIndex_.find(key);
        if (q == typedefIndex_.end()) return static_cast<const Type *>(nullptr);
        refuseInlineAmbiguity(key, q->second, typedefIndex_, &Parser::sameTypedef);
        return typedefs_[q->second].type;
    };
    const std::string nearer = enclosingNamespaceKey(name, &Parser::hasTypeNamed);
    if (!nearer.empty()) return typeAt(nearer);

    // A qualified name's first component is found innermost first, so a nearer
    // `inner::S` hides one at file scope before the name as written is tried.
    if (name.find("::") != std::string::npos && !namespaceStack_.empty()) {
        const std::string key = qualifyForLookup(name, &Parser::hasTypeNamed);
        if (key != name)
            if (const Type *t = typeAt(key)) return t;
    }

    if (const Type *t = typeAt(name)) return t;
    // **`N::D` where D is in N's unnamed namespace** - [namespace.unnamed]/1
    // gives N a using-directive for it, and a qualified name honours that.
    const std::size_t last = name.rfind("::");
    if (last != std::string::npos && last > 0) {
        auto u = typedefIndex_.find(name.substr(0, last) + "::_GLOBAL__N_1" + name.substr(last));
        if (u != typedefIndex_.end()) return typedefs_[u->second].type;
    }

    // A class named without its namespace, from inside that namespace or from
    // one a `using namespace` has opened.
    {
        const std::string key = qualifyForLookup(name, &Parser::hasTypeNamed);
        if (key != name)
            if (const Type *t = typeAt(key)) return t;
    }

    return classScopeType(name);
}

// **The classes this is inside, innermost first**: a class body being parsed, then the class whose
// member function's body this is - a nested class is unqualified only from inside.
const Type *Parser::classScopeType(const std::string &name) const {
    if (boundTypeParams_.count(name) != 0) return nullptr;      // a bound parameter hides it
    for (std::size_t i = classStack_.size(); i-- > 0; )
        if (const Type *t = lookupInClass(classStack_[i], name)) return t;
    if (currentClass_ != nullptr)
        if (const Type *t = lookupInClass(currentClass_, name)) return t;
    if (const Type *scope = lambdaScope())
        if (const Type *t = lookupInClass(scope, name)) return t;

    // **A held body is replayed at file scope, and its return type is read before
    // anything says which class it belongs to.** `Holder *self()` is the case, and
    // inlineOwner_ is the one thing that knows, so it is asked here.
    if (!inlineOwner_.empty()) {
        auto it = typedefIndex_.find(inlineOwner_);
        if (it != typedefIndex_.end())
            if (const Type *t = lookupInClass(typedefs_[it->second].type, name))
                return t;
    }
    if (!signatureOwner_.empty()) {
        auto it = typedefIndex_.find(signatureOwner_);
        if (it != typedefIndex_.end())
            if (const Type *t = lookupInClass(typedefs_[it->second].type, name))
                return t;
    }
    return nullptr;
}

// A class or enum name is a type name in C++ with no typedef written - inserted into the scope
// the definition appears in, the one table this parser has; an entry an inline namespace's member
// put under the key is not a declaration of it. Not implemented: an object hiding the class name.
void Parser::declareTypeName(const std::string &name, const Type *type) {
    auto it = typedefIndex_.find(name);
    const bool aliasHere = it != typedefIndex_.end() && typedefs_[it->second].name != name;
    if (!aliasHere && findTypedef(name) != nullptr) return;
    typedefIndex_[name] = typedefs_.size();
    typedefs_.push_back(TypedefName{ name, type });
    aliasTypeKey(name, typedefs_.size() - 1);
}

bool Parser::hasEnumNamed(const std::string &key) const {
    return enumIndex_.find(key) != enumIndex_.end();
}

const Parser::EnumConst *Parser::enumInClass(const Type *cls,
                                             const std::string &name) const {
    for (const Type *c = cls; c != nullptr; c = c->enclosing()) {
        auto it = enumIndex_.find(c->tag() + "::" + name);
        if (it != enumIndex_.end()) return &enums_[it->second];
        for (const Type::BaseSpec &b : c->bases())
            if (const EnumConst *e = enumInClass(b.type, name)) return e;
    }
    return nullptr;
}

// **An enumerator is named through what encloses it**, the same as a class -
// `Layout::StepBase`, `n::Red` - so the scopes findTypedef walks are walked
// here too.
const Parser::EnumConst *Parser::findEnum(const std::string &name) const {
    if (boundValueParams_.count(name) != 0) {
        auto bound = enumIndex_.find(name);
        if (bound != enumIndex_.end()) return &enums_[bound->second];
    }
    auto enumAt = [&](const std::string &key) {
        auto q = enumIndex_.find(key);
        if (q == enumIndex_.end()) return static_cast<const EnumConst *>(nullptr);
        refuseInlineAmbiguity(key, q->second, enumIndex_);
        return &enums_[q->second];
    };
    const std::string nearer = enclosingNamespaceKey(name, &Parser::hasEnumNamed);
    if (!nearer.empty()) return enumAt(nearer);
    if (const EnumConst *e = enumAt(name)) return e;

    const std::string key = qualifyForLookup(name, &Parser::hasEnumNamed);
    if (key != name)
        if (const EnumConst *e = enumAt(key)) return e;

    return findClassEnum(name);
}

// The class half on its own. An enumerator of the class being parsed is at
// class scope, so it beats a global and loses to a local - which the caller
// in `primary` needs to ask separately from the namespace-scope table above.
const Parser::EnumConst *Parser::findClassEnum(const std::string &name) const {
    if (boundValueParams_.count(name) != 0) return nullptr;     // a bound parameter hides it
    for (std::size_t i = classStack_.size(); i-- > 0; )
        if (const EnumConst *e = enumInClass(classStack_[i], name)) return e;
    if (currentClass_ != nullptr)
        if (const EnumConst *e = enumInClass(currentClass_, name)) return e;
    if (const Type *scope = lambdaScope())
        if (const EnumConst *e = enumInClass(scope, name)) return e;
    if (!inlineOwner_.empty()) {
        auto owner = typedefIndex_.find(inlineOwner_);
        if (owner != typedefIndex_.end())
            if (const EnumConst *e =
                    enumInClass(typedefs_[owner->second].type, name))
                return e;
    }
    return nullptr;
}

// `Point::Point(`, `Outer::Inner::Inner(` and `Outer::Inner::~Inner(` - a
// member definition whose name is its class's own, and so has no type in
// front of it. Every level is asked, because the class may itself be nested.
bool Parser::atUntypedMemberDefinition() const {
    if (peek().kind != TokenKind::Ident || !peekAt(1).is("::")) return false;
    std::string q = peek().text;
    for (std::size_t k = 1; peekAt(k).is("::"); ) {
        std::size_t n = k + 1;
        const bool destructor = peekAt(n).is("~");
        if (destructor) n++;
        if (peekAt(n).kind != TokenKind::Ident) return false;
        const std::string component = peekAt(n).text;
        const Type *cls = findTypedef(q);
        if (cls != nullptr && cls->isStructOrUnion() &&
            cls->localName() == component && peekAt(n + 1).is("("))
            return true;
        if (destructor) return false;
        q += "::" + component;
        k = n + 1;
    }
    return false;
}

bool Parser::atTypeName() const {
    static const char *const t[] = { "void", "bool", "char", "short", "int",
                                     "long", "signed", "unsigned", "wchar_t",
                                     "char16_t", "char32_t",
                                     "float", "double",
                                     "struct", "union", "enum",
                                     "const", "volatile",
                                     // It says the next thing is a type, and saying so is the whole of what it does.
                                     "typename",
                                     // And this one *is* a type, written as a
                                     // question about an expression.
                                     "decltype", "__underlying_type" };
    for (const char *k : t)
        if (peek().is(k)) return true;
    // `::Lexer *p;` declares a p - the leading `::` says where to look and
    // nothing else, so the question is about the name after it.
    if (peek().is("::") && peekAt(1).kind == TokenKind::Ident) {
        const std::map<std::string, TemplateDecl>::const_iterator t = findTemplate("::" + peekAt(1).text);
        if (peekAt(2).is("<") && t != templates_.end() && t->second.isClass && t->second.ns.empty()) return true;
        return findGlobalTypedef(peekAt(1).text) != nullptr;
    }
    if (peek().kind != TokenKind::Ident) return false;
    if (localHidesType()) return false;
    // `Box<int> b;` declares a variable, so the name has to answer yes here -
    // but only with a `<` after it, since the bare name is not a type.
    auto tmpl = findTemplate(peek().text);
    if (tmpl != templates_.end() && tmpl->second.isClass && peekAt(1).is("<"))
        return true;
    if (peekAt(1).is("<") && memberClassTemplate(peek().text) != nullptr) return true;
    if (findTypedef(peek().text) != nullptr) return true;
    // `N::S` names a type as `Outer::Inner` does: the same walk past `N`
    return qualifiedTypeEnd() != 0;
}

// How many tokens of an `A::B::C` chain starting here reach a type, or 0 if
// none does. The longest prefix that names one wins, so `Outer::Inner::shared`
// stops at Inner and `N::M::S` runs to the end.
std::size_t Parser::qualifiedTypeEnd() const {
    if (peek().kind != TokenKind::Ident || !peekAt(1).is("::")) return 0;
    std::string q = peek().text;
    std::size_t typeEnd = 0;
    const Type *cur = findTypedef(q);
    for (std::size_t k = 1; peekAt(k).is("::") &&
                            peekAt(k + 1).kind == TokenKind::Ident;
         k += 2) {
        q += "::" + peekAt(k + 1).text;
        const Type *outer = cur;
        if (const Type *t = findTypedef(q)) { typeEnd = k + 2; cur = t; }
        // `std::string::size_type`: a member type of the class a typedef reached.
        else if (cur != nullptr && cur->isStructOrUnion() &&
                 (cur = lookupInClass(cur, peekAt(k + 1).text)) != nullptr)
            typeEnd = k + 2;
        // **`std::vector<int> v;` - the name stops at a class template too**, and at a member one, `S::In<int>`.
        if (peekAt(k + 2).is("<") &&
            (isClassTemplate(q) || memberClassTemplateOf(outer, peekAt(k + 1).text) != nullptr))
            typeEnd = k + 2;
    }
    return typeEnd;
}

const Type *Parser::qualifiedTypeAt(std::size_t end) const {
    std::string q = peek().text;
    const Type *cur = findTypedef(q);
    for (std::size_t k = 1; k + 1 < end; k += 2) {
        q += "::" + peekAt(k + 1).text;
        if (const Type *t = findTypedef(q)) cur = t;
        else cur = cur != nullptr && cur->isStructOrUnion()
                       ? lookupInClass(cur, peekAt(k + 1).text) : nullptr;
    }
    return cur;
}

std::size_t Parser::qualifiedTypeEndPastArgs() const {
    std::size_t after = qualifiedTypeEnd();
    if (after == 0 || !peekAt(after).is("<")) return after;
    // By depth, counting a `>>` as two - which is how the lexer hands it over.
    int depth = 0;
    for (;; after++) {
        const Token &t = peekAt(after);
        if (t.kind == TokenKind::End) return 0;
        if (depth > 0 && (t.is("(") || t.is("["))) { after = groupCloseAt(at_ + after) - at_; continue; }
        if (t.is("<")) depth++;
        else if (t.is(">")) { if (--depth == 0) return after + 1; }
        else if (t.is(">>")) { depth -= 2; if (depth <= 0) return after + 1; }
    }
}

// [stmt.ambig]/1: `T(x).f();` cannot be a declaration - no declarator goes on with `.` or `->`
// after its `)` - so it is a temporary of T and a member call. True when that is what is here.
// Where `T(...)` opening the statement closes: the index of its `)`, or 0 where it does not open so.
std::size_t Parser::temporaryCallEnd(std::size_t *open) const {
    if (peek().kind != TokenKind::Ident) return 0;
    std::size_t k = 0;
    if (peekAt(1).is("::")) k = qualifiedTypeEndPastArgs();
    else if (localHidesType()) return 0;
    else if (peekAt(1).is("<") && isClassTemplate(peek().text)) {
        int depth = 0;
        for (k = 1;; k++) {
            const Token &t = peekAt(k);
            if (t.kind == TokenKind::End || t.is(";")) return 0;
            if (depth > 0 && (t.is("(") || t.is("["))) { k = groupCloseAt(at_ + k) - at_; continue; }
            if (t.is("<")) depth++;
            else if (t.is(">")) { if (--depth == 0) { k++; break; } }
            else if (t.is(">>")) { depth -= 2; if (depth <= 0) { k++; break; } }
        }
    } else if (findTypedef(peek().text) != nullptr) k = 1;
    if (k == 0 || !peekAt(k).is("(")) return 0;
    if (open != nullptr) *open = k;
    int depth = 0;
    for (;; k++) {
        const Token &t = peekAt(k);
        if (t.kind == TokenKind::End) return 0;
        if (t.is("(") || t.is("[") || t.is("{")) depth++;
        else if (t.is(")") || t.is("]") || t.is("}")) { if (--depth == 0) return k; }
    }
}

// [stmt.ambig]/1 again: `T(4);` and `T(a + b);` hold nothing a declarator could be, so they are temporaries.
bool Parser::atTemporaryOfArguments() const {
    std::size_t open = 0;
    const std::size_t close = temporaryCallEnd(&open);
    if (close <= open + 1) return false;
    int depth = 0;
    for (std::size_t i = open + 1; i < close; i++) {
        const Token &t = peekAt(i);
        if (t.is("(") || t.is("[") || t.is("{")) depth++;
        else if (t.is(")") || t.is("]") || t.is("}")) depth--;
        else if (depth == 0 && t.kind != TokenKind::Ident && !t.is("*") && !t.is("&") && !t.is("&&") &&
                 !t.is("::") && !t.is("<") && !t.is(">") && !t.is("...") && !t.is("const") && !t.is("volatile"))
            return true;
    }
    return false;
}

bool Parser::atTemporaryMemberAccess() const {
    const std::size_t k = temporaryCallEnd();
    return k != 0 && (peekAt(k + 1).is(".") || peekAt(k + 1).is("->"));
}

// **[stmt.select]/1: a condition declares only where `=` or `{` follows its declarator**, so after
// `T(x)` anything else - `==`, `<`, `&&`, `)` - makes `T(x)` a temporary and the condition an expression.
bool Parser::atConditionDeclaration() const {
    if (!atDeclarationStart()) return false;
    const std::size_t k = temporaryCallEnd();
    return k == 0 || peekAt(k + 1).is("=") || peekAt(k + 1).is("{");
}

bool Parser::atDeclarationStart() const {
    if (atTemporaryMemberAccess() || atTemporaryOfArguments()) return false;
    // **`Counter::total = 1;` is a statement, not a declaration**, though it opens
    // with a name that names a type: a declaration spelled `C::something` needs a
    // nested class, so a class name before '::' always begins an expression here.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("::")) {
        const Type *named = findTypedef(peek().text);
        // A namespace comes here for the same reason a class does: `N::v = 1;`
        // is a statement and `N::S s;` is a declaration, and only the walk
        // below tells them apart.
        if ((named != nullptr && named->isStructOrUnion()) ||
            !namespaceNamed(peek().text).empty()) {
            // ...**unless the qualified name is itself a type**: it declares only
            // where the name *stops* at one. `Outer::Inner x;` declares an x;
            // `Outer::Inner::shared = 1;` goes on past the type and assigns.
            const std::size_t typeEnd = qualifiedTypeEnd();
            if (typeEnd == 0 || peekAt(typeEnd).is("::")) return false;
            // **`std::vector<int>().swap(v);` is a statement**, and an empty pair after the type is
            // what says so: a declaration needs a name between the type and the `(`, so `T ();`
            // could only be a function declaration with no name, which is not a thing.
            const std::size_t after = qualifiedTypeEndPastArgs();
            if (after == 0) return true;
            // `std::vector<bool>::swap(r0, r1);` goes on past the template-id to a member, and is a
            // declaration only where a declarator follows that member - as for `Tm<P>::st.a` below.
            if (peekAt(after).is("::") && peekAt(after + 1).kind == TokenKind::Ident) {
                const Token &next = peekAt(after + 2);
                return next.kind == TokenKind::Ident || next.is("*") || next.is("&") || next.is("&&") ||
                       next.is("::") || next.is("<");
            }
            return !(peekAt(after).is("(") && peekAt(after + 1).is(")"));
        }
    }

    // **A leading class template-id as a qualifier**:
    // `CNeeds<(N==3)>::check()` is a static-member call, a statement, where
    // `CNeeds<3>::Inner x;` would be a declaration.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("<") &&
        isClassTemplate(peek().text)) {
        std::size_t after = 1;
        int depth = 0;
        for (;; after++) {
            const Token &t = peekAt(after);
            if (t.kind == TokenKind::End) { after = 0; break; }
            if (depth > 0 && (t.is("(") || t.is("["))) { after = groupCloseAt(at_ + after) - at_; continue; }
            if (t.is("<")) depth++;
            else if (t.is(">")) { if (--depth == 0) { after++; break; } }
            else if (t.is(">>")) { depth -= 2; if (depth <= 0) { after++; break; } }
        }
        // And `Tm<P>::st.a = 3;` names a static member: a declaration goes on
        // from the member with a declarator - a name, `*`, `&`, `::` or `<` -
        // and anything else is an expression built on it (the cl review's A13).
        if (after != 0 && peekAt(after).is("::") &&
            peekAt(after + 1).kind == TokenKind::Ident) {
            const Token &next = peekAt(after + 2);
            if (next.kind != TokenKind::Ident && !next.is("*") && !next.is("&") &&
                !next.is("&&") && !next.is("::") && !next.is("<"))
                return false;
        }
    }

    // The same empty pair, for a type named without a qualifier: `P().get();`
    // is the temporary and not a declaration of anything.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("(") &&
        peekAt(2).is(")") && findTypedef(peek().text) != nullptr)
        return false;
    // And for a type a keyword names: `void();` declares nothing, so it is [expr.type.conv]'s prvalue.
    if (simpleTypeKeyword() != nullptr && peekAt(1).is("(") && peekAt(2).is(")"))
        return false;

    // `constexpr` beside the storage classes, and here rather than in atTypeName:
    // it is a decl-specifier that names no type, so a statement starting with it
    // is a declaration and nothing else - or an expression is asked for instead.
    return atTypeName() || peek().is("static") || peek().is("extern")
        || peek().is("register") || peek().is("auto") || peek().is("typedef")
        || peek().is("constexpr") || peek().is("alignas") || peek().is("class")
        || peek().is("thread_local")
        || (peek().is("[") && peekAt(1).is("["));
}

// From the '{' to the '}' that closes it, counting depth.
void Parser::skipBracedBlock() {
    // A function try block's handlers are its body too, [dcl.fct.def.general]/1.
    const bool functionTry = peek().is("try");
    // A constructor's body may follow its mem-initializers: skip from the
    // ':' to the '{' **past each entry's own, maybe braced, arguments**
    // (`: i{v}, d{1.5} {}` held `{v}` as the body).
    if (functionTry && peekAt(1).is(":")) at_++;
    if (peek().is(":")) {
        at_++;
        auto skipGroup = [&]() {
            int depth = 0;
            do {
                if (peek().kind == TokenKind::End)
                    src_.fail(peek().pos, "this mem-initialiser is never closed");
                if (peek().is("(") || peek().is("{") || peek().is("[")) depth++;
                else if (peek().is(")") || peek().is("}") || peek().is("]")) depth--;
                at_++;
            } while (depth > 0);
        };
        for (;;) {
            int angle = 0;
            while (angle > 0 || (!peek().is("(") && !peek().is("{"))) {
                if (peek().kind == TokenKind::End)
                    src_.fail(peek().pos, "this member function has no body after all");
                if (peek().is("<")) angle++;
                else if (peek().is(">")) angle--;
                else if (peek().is(">>")) angle -= 2;
                else if (angle > 0 && (peek().is("(") || peek().is("["))) { skipGroup(); continue; }
                if (angle < 0) angle = 0;
                at_++;
            }
            skipGroup();
            consume("...");
            if (!consume(",")) break;
        }
    }
    while (!peek().is("{")) {
        if (peek().kind == TokenKind::End)
            src_.fail(peek().pos, "this member function has no body after all");
        at_++;
    }
    int depth = 0;
    for (;;) {
        if (peek().kind == TokenKind::End)
            src_.fail(peek().pos, "this member function's body is never closed");
        if (peek().is("{")) depth++;
        else if (peek().is("}")) {
            depth--;
            at_++;
            if (depth == 0 && !(functionTry && peek().is("catch"))) return;
            continue;
        }
        at_++;
    }
}

// Each held body re-read as though it had been written outside the class. The
// tokens are the ones already there, so the ordinary definition path runs over
// them, and `inlineOwner_` supplies the `Class::` the source does not have.
Parser::FunctionState Parser::captureFunctionState() const {
    FunctionState s;
    s.at = at_;
    s.currentFunction = currentFunction_;
    s.currentFunctionName = currentFunctionName_;
    s.functionName = functionName_;
    s.localTypes = localTypes_;
    s.locals = locals_;
    s.fnVars = fnVars_;
    s.scopeStarts = scopeStarts_;
    s.blocks = blocks_;
    s.blockStack = blockStack_;
    s.labels = labels_;
    s.gotos = gotos_;
    s.frameSize = frameSize_;
    s.frameAlign = frameAlign_;
    s.freeSlots = freeSlots_;
    s.localSerial = localSerial_;
    s.thisOffset = thisOffset_;
    s.currentClass = currentClass_;
    s.friendScope = friendScope_;
    s.returnType = returnType_;
    s.sretSlot = sretSlot_;
    s.nrvoSlot = nrvoSlot_;
    s.nrvoName = nrvoName_;
    s.deducingReturn = deducingReturn_;
    s.unevaluated = unevaluated_;
    s.lambdaCount = lambdaCount_;
    s.atFunctionBody = atFunctionBody_;
    s.alive = alive_;
    s.pendingTemps = pendingTemps_;
    s.statementTemps = statementTemps_;
    s.guardSlots = guardSlots_;
    s.bodyCleanupFrom = bodyCleanupFrom_;
    s.functionHasPads = functionHasPads_;
    s.functionHasTry = functionHasTry_;
    s.functionTypes = functionTypes_;
    s.variadicBody = variadicBody_;
    s.inStaticMember = inStaticMember_;
    s.inParams = inParams_;
    s.staticSymbols = staticSymbols_;
    s.pendingDefaults = pendingDefaults_;
    s.pendingNoexcept = pendingNoexcept_;
    s.pendingSpec = pendingSpec_;
    s.inNoexceptFunction = inNoexceptFunction_;
    s.dtorEndLabel = dtorEndLabel_;
    s.dtorReturned = dtorReturned_;
    s.replayingInline = replayingInline_;
    s.currentFunctionInline = currentFunctionInline_;
    s.tssSeq = tssSeq_;
    s.loopDepth = loopDepth_;
    s.switchDepth = switchDepth_;
    s.switches = switches_;
    s.breakMarks = breakMarks_;
    s.inTryBody = inTryBody_;
    s.inMsHandler = inMsHandler_;
    s.msExit = msExit_;
    s.inHandlerBody = inHandlerBody_;
    s.inCtorTryHandler = inCtorTryHandler_;
    s.handlerDepth = handlerDepth_;
    s.handlerLoopDepth = handlerLoopDepth_;
    s.handlerSwitchDepth = handlerSwitchDepth_;
    s.handlerFrom = handlerFrom_;
    s.tryFrom = tryFrom_;
    s.handlerResumeLabel = handlerResumeLabel_;
    s.handlerResumePtr = handlerResumePtr_;
    s.handlerResumeSel = handlerResumeSel_;
    s.tryChainLabel = tryChainLabel_;
    s.tryChainPointerSlot = tryChainPointerSlot_;
    s.tryChainSelectorSlot = tryChainSelectorSlot_;
    s.tryChainAliveFrom = tryChainAliveFrom_;
    s.handlerAliveFrom = handlerAliveFrom_;
    s.tryBodySegments = tryBodySegments_;
    s.tryBodySerial = tryBodySerial_;
    s.mayThrow = mayThrow_;
    s.conditionDecl = conditionDecl_;
    s.conditionName = conditionName_;
    s.usingNamespaces = usingNamespaces_;
    return s;
}

void Parser::restoreFunctionState(const FunctionState &s) {
    at_ = s.at;
    currentFunction_ = s.currentFunction;
    currentFunctionName_ = s.currentFunctionName;
    functionName_ = s.functionName;
    localTypes_ = s.localTypes;
    locals_ = s.locals;
    fnVars_ = s.fnVars;
    scopeStarts_ = s.scopeStarts;
    blocks_ = s.blocks;
    blockStack_ = s.blockStack;
    labels_ = s.labels;
    gotos_ = s.gotos;
    frameSize_ = s.frameSize;
    frameAlign_ = s.frameAlign;
    freeSlots_ = s.freeSlots;
    localSerial_ = s.localSerial;
    thisOffset_ = s.thisOffset;
    currentClass_ = s.currentClass;
    friendScope_ = s.friendScope;
    returnType_ = s.returnType;
    sretSlot_ = s.sretSlot;
    nrvoSlot_ = s.nrvoSlot;
    nrvoName_ = s.nrvoName;
    deducingReturn_ = s.deducingReturn;
    unevaluated_ = s.unevaluated;
    lambdaCount_ = s.lambdaCount;
    atFunctionBody_ = s.atFunctionBody;
    alive_ = s.alive;
    pendingTemps_ = s.pendingTemps;
    statementTemps_ = s.statementTemps;
    guardSlots_ = s.guardSlots;
    bodyCleanupFrom_ = s.bodyCleanupFrom;
    functionHasPads_ = s.functionHasPads;
    functionHasTry_ = s.functionHasTry;
    functionTypes_ = s.functionTypes;
    variadicBody_ = s.variadicBody;
    inStaticMember_ = s.inStaticMember;
    inParams_ = s.inParams;
    staticSymbols_ = s.staticSymbols;
    pendingDefaults_ = s.pendingDefaults;
    pendingNoexcept_ = s.pendingNoexcept;
    pendingSpec_ = s.pendingSpec;
    inNoexceptFunction_ = s.inNoexceptFunction;
    dtorEndLabel_ = s.dtorEndLabel;
    dtorReturned_ = s.dtorReturned;
    replayingInline_ = s.replayingInline;
    currentFunctionInline_ = s.currentFunctionInline;
    tssSeq_ = s.tssSeq;
    loopDepth_ = s.loopDepth;
    switchDepth_ = s.switchDepth;
    switches_ = s.switches;
    breakMarks_ = s.breakMarks;
    inTryBody_ = s.inTryBody;
    inMsHandler_ = s.inMsHandler;
    msExit_ = s.msExit;
    inHandlerBody_ = s.inHandlerBody;
    inCtorTryHandler_ = s.inCtorTryHandler;
    handlerDepth_ = s.handlerDepth;
    handlerLoopDepth_ = s.handlerLoopDepth;
    handlerSwitchDepth_ = s.handlerSwitchDepth;
    handlerFrom_ = s.handlerFrom;
    tryFrom_ = s.tryFrom;
    handlerResumeLabel_ = s.handlerResumeLabel;
    handlerResumePtr_ = s.handlerResumePtr;
    handlerResumeSel_ = s.handlerResumeSel;
    tryChainLabel_ = s.tryChainLabel;
    tryChainPointerSlot_ = s.tryChainPointerSlot;
    tryChainSelectorSlot_ = s.tryChainSelectorSlot;
    tryChainAliveFrom_ = s.tryChainAliveFrom;
    handlerAliveFrom_ = s.handlerAliveFrom;
    tryBodySegments_ = s.tryBodySegments;
    tryBodySerial_ = s.tryBodySerial;
    mayThrow_ = s.mayThrow;
    conditionDecl_ = s.conditionDecl;
    conditionName_ = s.conditionName;
    usingNamespaces_ = s.usingNamespaces;
}

// A body parsed behind one of these doors is a *different* function, so it
// starts with none of the enclosing one's state rather than a copy of it.
// `at_` is left alone: the caller has just pointed it at what to read.
void Parser::clearFunctionState() {
    currentFunction_.clear();
    currentFunctionName_.clear();
    functionName_.clear();
    localTypes_.clear();
    locals_.clear();
    fnVars_.clear();
    scopeStarts_.clear();
    blocks_.clear();
    blockStack_.clear();
    labels_.clear();
    gotos_.clear();
    frameSize_ = 0;
    frameAlign_ = 16;
    freeSlots_.clear();
    localSerial_ = 0;
    thisOffset_ = 0;
    currentClass_ = nullptr;
    friendScope_ = nullptr;
    returnType_ = nullptr;
    sretSlot_ = 0;
    nrvoSlot_ = 0;
    nrvoName_.clear();
    deducingReturn_ = nullptr;
    lambdaCount_ = 0;
    unevaluated_ = 0;
    atFunctionBody_ = false;
    alive_.clear();
    pendingTemps_.clear();
    statementTemps_.clear();
    guardSlots_.clear();
    bodyCleanupFrom_ = 0;
    functionHasPads_ = false;
    functionHasTry_ = false;
    functionTypes_.clear();
    variadicBody_ = false;
    inStaticMember_ = false;
    inParams_ = false;
    staticSymbols_.clear();
    pendingDefaults_.clear();
    pendingNoexcept_ = false;
    pendingSpec_ = DynamicSpec();
    inNoexceptFunction_ = false;
    dtorEndLabel_.clear();
    currentFunctionInline_ = false;
    tssSeq_ = 0;
    loopDepth_ = 0;
    switchDepth_ = 0;
    switches_.clear();
    breakMarks_.clear();
    inTryBody_ = false;
    inMsHandler_ = false;
    msExit_ = nullptr;
    inHandlerBody_ = false;
    inCtorTryHandler_ = false;
    handlerDepth_ = 0;
    handlerLoopDepth_.clear();
    handlerSwitchDepth_.clear();
    handlerFrom_.clear();
    tryFrom_.clear();
    handlerResumeLabel_.clear();
    handlerResumePtr_ = 0;
    handlerResumeSel_ = 0;
    tryChainLabel_.clear();
    tryChainPointerSlot_ = 0;
    tryChainSelectorSlot_ = 0;
    tryChainAliveFrom_ = 0;
    handlerAliveFrom_ = 0;
    tryBodySegments_.clear();
    tryBodySerial_ = -1;
    mayThrow_ = 0;
    conditionDecl_ = false;
    conditionName_.clear();
}

void Parser::replayInlineBodies(std::vector<PendingBody> mine) {
    if (mine.empty()) return;
    // **One capture for the whole nested parse.**
    const FunctionState outer = captureFunctionState();
    // The packs too: a body read here declaring a parameter `a` drops a function parameter pack `a` of
    // the function it was asked for from - deque's emplace lost its `a` to allocator_traits::construct's.
    const std::map<std::string, PackBinding> outerPacks = packs_;
    clearFunctionState();
    // A local class's bodies are replayed inside the function that wrote it, where its name and
    // its neighbours' are the function's local type names (1.27, 2026-10-21, review of 1.25, item 3).
    localTypes_ = outer.localTypes;
    // A body replayed here was written inside a class body, so every definition it produces is implicitly inline.
    replayingInline_ = true;
    const bool wasFriend = friendDeclaration_;
    const std::vector<std::string> wasNamespace = namespaceStack_;

    // **The outer function comes back on every way out**: a member template instantiated
    // inside a trial may throw from here, and the call that follows still reads its locals.
    try {
    for (std::size_t i = 0; i < mine.size(); i++) {
        at_ = mine[i].start;
        const ReadingAt reading(this, &memberAt_);
        // A friend's body is written inside the class and belongs outside it,
        // so it is replayed with no owner to supply a `Class::` it must not have.
        inlineOwner_ = mine[i].freeFunction ? std::string() : mine[i].tag;
        inlineOwnerName_ = mine[i].freeFunction ? std::string()
                         : (mine[i].local.empty() ? mine[i].tag : mine[i].local);
        // **A friend is not a member, but it is still looked up in the
        // class.**
        pendingDefaults_.clear();
        pendingNoexcept_ = false;
        pendingSpec_ = DynamicSpec();
        // Each body is a function of its own: nothing the one before left pending is this one's to destroy, at
        // offsets of this frame (1.81).
        pendingTemps_.clear();
        statementTemps_.clear();
        guardSlots_.clear();
        const bool scoped = mine[i].freeFunction && !mine[i].tag.empty();
        const Type *scope = scoped ? findTypedef(mine[i].tag) : nullptr;
        if (scope != nullptr) classStack_.push_back(scope);
        friendDeclaration_ = mine[i].freeFunction;
        if (mine[i].freeFunction) namespaceStack_ = mine[i].friendNamespace;
        friendScope_ = scope != nullptr && scope->isStructOrUnion() ? scope->unqualified() : nullptr;
        topLevel(*current_);
        friendScope_ = nullptr;
        namespaceStack_ = wasNamespace;
        friendDeclaration_ = wasFriend;
        if (scope != nullptr) classStack_.pop_back();
        inlineOwner_.clear();
        inlineOwnerName_.clear();
    }
    } catch (...) {
        friendDeclaration_ = wasFriend;
        namespaceStack_ = wasNamespace;
        inlineOwner_.clear();
        inlineOwnerName_.clear();
        restoreFunctionState(outer);
        packs_ = outerPacks;
        throw;
    }
    restoreFunctionState(outer);
    packs_ = outerPacks;
}

// **One level of the program's nesting is one level of the parser's**, and the limit is said
// where it is reached, as the '#include' and constant-expression depths are: a bound named is
// better than a stack that is not, and the standard lets an implementation set one.
Parser::Nesting::Nesting(Parser *pp, std::size_t pos, const char *what) : p(pp) {
    if (++p->nesting_ <= kMaxNesting) return;
    p->nesting_--;
    p->src_.fail(pos, std::string("this ") + what + " is nested more than " +
                      std::to_string(kMaxNesting) + " levels deep - parentheses, blocks, "
                      "statements, lambdas and template instantiations counted together - and "
                      "the compiler, which follows the program's nesting with its own, stops "
                      "here rather than run out of stack");
}

void Parser::enterScope() { scopeStarts_.push_back(locals_.size()); }

int Parser::enterBlock() {
    blocks_.push_back(currentBlock());
    int id = static_cast<int>(blocks_.size()) - 1;
    blockStack_.push_back(id);
    return id;
}

void Parser::leaveBlock() { blockStack_.pop_back(); }

// **A scope's frame slots are given back as it closes**, for a later local of exactly the type stored there: the
// objects are dead ([basic.life]), nothing but a sibling scope can declare into the gap, and one type per slot is
// what every backend's slot-keyed bookkeeping - the C6000's register planner, the hosts' promotion - already assumes.
void Parser::leaveScope() {
    for (std::size_t i = scopeStarts_.back(); i < locals_.size(); i++) {
        const Local &l = locals_[i];
        if (l.offset <= 0 || !l.staticName.empty() || l.isParameter || inParams_ || l.sharesSlot) continue;
        const Type *stored = l.type->isReference() ? types_.pointerTo(l.type->referent()) : l.type;
        freeSlots_.push_back(FreeSlot{ stored, l.offset, objectAlign(stored, target_) });
    }
    locals_.resize(scopeStarts_.back());
    scopeStarts_.pop_back();
}

// A freed slot belongs to the frame that freed it: a function opened mid-parse starts with none, or it reuses an offset its own frame never reached.
Parser::FrameSave Parser::openFrame() {
    FrameSave saved{ frameSize_, std::move(freeSlots_) };
    frameSize_ = 0;
    freeSlots_.clear();
    return saved;
}

void Parser::closeFrame(const FrameSave &saved) {
    frameSize_ = saved.size;
    freeSlots_ = saved.free;
}

int Parser::allocateFrameSlot(const Type *type, int alignAtLeast) {
    // What a reference occupies is a pointer, even though sizeof asks about
    // what it refers to. This is the one place the difference shows.
    const Type *stored = type->isReference()
                       ? types_.pointerTo(type->referent()) : type;
    int align = objectAlign(stored, target_);
    if (alignAtLeast > align) align = alignAtLeast;
    frameSize_ += stored->size(target_);
    frameSize_ = alignTo(frameSize_, align);
    // Past the stack's alignment the whole frame is realigned to the widest slot, by its symbol.
    if (align > frameAlign_) {
        frameAlign_ = align;
        const std::string &owner = currentFunction_.empty() ? functionName_ : currentFunction_;
        if (frameAligns_[owner] < align) frameAligns_[owner] = align;
    }
    return frameSize_;
}

int Parser::declare(const std::string &name, const Type *type, std::size_t pos,
                    int alignAtLeast) {
    refuseWeakAlignas(alignAtLeast, type, pos);
    // A frame slot is only as aligned as the stack it hangs from; past that the
    // frame is realigned by the hosts' prologues, and the C6000's is not yet.
    const int typeAlign = type->isReference() ? 0 : type->align(target_);
    if ((alignAtLeast > target_.stackAlign() || typeAlign > target_.stackAlign()) && !target_.realignsFrame())
        src_.fail(pos, "'alignas(" + std::to_string(alignAtLeast > typeAlign ? alignAtLeast : typeAlign) + ")' on a "
                       "local is not supported yet on " + target_.name() + ": the stack is kept " +
                       std::to_string(target_.stackAlign()) + "-aligned there, and a local past that "
                       "would need the frame realigned, which that backend does not do. A global or "
                       "a member may ask for more");
    if (type->isVoid())
        src_.fail(pos, "'" + name + "' cannot have type void");
    std::size_t from = scopeStarts_.empty() ? 0 : scopeStarts_.back();
    for (std::size_t i = from; i < locals_.size(); i++)
        if (locals_[i].name == name)
            src_.fail(pos, "'" + name + "' is declared twice in this block");
    // [basic.scope.block]/2: the outermost block of a function body is the parameters' scope too.
    if (bodyScope_ != 0 && scopeStarts_.size() == bodyScope_ && !inParams_)
        for (std::size_t i = 0; i < from; i++)
            if (locals_[i].isParameter && locals_[i].name == name)
                src_.fail(pos, "'" + name + "' redeclares a parameter in the outermost block of the "
                               "function - [basic.scope.block]/2");

    int offset = 0;
    const Type *storedType = type->isReference() ? types_.pointerTo(type->referent()) : type;
    for (std::size_t i = freeSlots_.size(); i-- > 0 && offset == 0;)
        if (freeSlots_[i].stored == storedType && freeSlots_[i].align >= alignAtLeast) { offset = freeSlots_[i].offset; freeSlots_.erase(freeSlots_.begin() + static_cast<long>(i)); }
    if (offset == 0) offset = allocateFrameSlot(type, alignAtLeast);
    if (type->kind() != Kind::PackExpansion) dropStalePack(name);
    locals_.push_back(Local{ name, offset, type, false, std::string() });
    locals_.back().isParameter = inParams_;
    locals_.back().noexceptPointer = fnPointerSpec_ == 1;
    locals_.back().serial = ++localSerial_;
    // The debug record describes the storage, which for a reference is the
    // pointer it really is. DWARF has a tag for a reference and this does not
    // use it yet.
    const Type *stored = type->isReference()
                       ? types_.pointerTo(type->referent()) : type;
    fnVars_.push_back(::Local{ name, stored, offset, inParams_, std::string(),
                              currentBlock() });
    fnVars_.back().align = alignAtLeast > objectAlign(stored, target_) ? alignAtLeast : objectAlign(stored, target_);
    return offset;
}

void Parser::dropStalePack(const std::string &name) {
    const auto pk = packs_.find(name);
    if (pk != packs_.end() && pk->second.functionParameter) packs_.erase(pk);
}

void Parser::declareStaticLocal(const std::string &name, const Type *type,
                                std::size_t pos, const std::string &symbol) {
    if (type->isVoid())
        src_.fail(pos, "'" + name + "' cannot have type void");
    std::size_t from = scopeStarts_.empty() ? 0 : scopeStarts_.back();
    for (std::size_t i = from; i < locals_.size(); i++)
        if (locals_[i].name == name)
            src_.fail(pos, "'" + name + "' is declared twice in this block");
    locals_.push_back(Local{ name, 0, type, false, symbol });
    fnVars_.push_back(::Local{ name, type, 0, false, symbol, currentBlock() });
}

// A thread_local has no address a debug record could hold - DWARF reaches one through the thread
// pointer, which these records do not write yet - so its static-local record is withdrawn.
void Parser::markThreadLocal(std::size_t local, bool threadLocal) {
    if (!threadLocal) return;
    locals_[local].isThreadLocal = true;
    for (std::size_t i = fnVars_.size(); i-- > 0;)
        if (fnVars_[i].staticName == locals_[local].staticName) { fnVars_.erase(fnVars_.begin() + static_cast<long>(i)); break; }
}

void Parser::tlsVar(Var *v, bool viaWrapper, bool external) {
    v->setThreadLocal(true);
    // Inside __tls_init an object of this unit is reached directly, as clang reaches it: the
    // function is already running, and on Windows its guard would call it again.
    if (currentFunction_ == "__tls_init" && tlsHere_.count(v->symbol()) != 0) viaWrapper = false;
    v->setViaWrapper(viaWrapper);
    if (external) tlsReached_.insert(v->symbol());
}

void Parser::tlsDefining(const std::string &symbol, bool external) {
    tlsHere_.insert(symbol);
    if (external) tlsDefined_.insert(symbol);
}

void Parser::refuseThreadLocalHere(bool written, std::size_t pos) {
    if (written) src_.fail(pos, "'thread_local' is only allowed on variable declarations");
}

void Parser::noteThreadLocal(GlobalSym &g, bool prior, bool threadLocal, const std::string &name,
                             bool viaWrapper, bool external, bool defines, std::size_t pos) {
    if (prior && g.isThreadLocal != threadLocal)
        src_.fail(pos, std::string(threadLocal ? "thread-local" : "non-thread-local") + " declaration of '" +
                       name + "' follows " + (threadLocal ? "non-thread-local" : "thread-local") + " declaration");
    g.isThreadLocal = threadLocal;
    if (!threadLocal) return;
    g.tlsViaWrapper = viaWrapper;
    g.tlsExternal = external;
    if (!defines) return;
    tlsDefining(g.symbol, external);
    if (viaWrapper && external) tlsDynamic_.insert(g.symbol);
    if (viaWrapper && !external) tlsInternalDynamic_.insert(g.symbol);
}

// [basic.scope.hiding]/2: `void f(tm *tm)` - in the body the parameter hides the class, so `tm`
// is an object there; a name before `::` still looks at types only, [basic.lookup.qual]/1.
bool Parser::localHidesType() const {
    if (peekAt(1).is("::") || localTypes_.count(peek().text) != 0) return false;
    if (findLocal(peek().text) != nullptr) return true;
    // [basic.scope.hiding]/2: a function declared in the class's own scope under its name hides
    // it, so `stat(p, &st)` beside `struct stat` is a call; `struct stat` still names the class.
    const Type *named = findTypedef(peek().text);
    if (named == nullptr || !named->isStructOrUnion() || named->localName() != peek().text) return false;
    for (std::size_t i = 0; i < classStack_.size(); i++)
        if (classStack_[i]->unqualified() == named->unqualified()) return false;
    if (currentClass_ != nullptr && currentClass_->unqualified() == named->unqualified()) return false;
    return hasFunctionNamed(named->tag());
}

const Parser::Local *Parser::findLocal(const std::string &name) const {
    for (std::size_t i = locals_.size(); i-- > 0; )
        if (locals_[i].name == name) return &locals_[i];
    return nullptr;
}

const Parser::GlobalSym *Parser::findGlobal(const std::string &name) const {
    auto globalAt = [&](const std::string &key) {
        auto q = globalIndex_.find(key);
        if (q == globalIndex_.end()) return static_cast<const GlobalSym *>(nullptr);
        refuseInlineAmbiguity(key, q->second, globalIndex_);
        return &globals_[q->second];
    };
    const std::string nearer = enclosingNamespaceKey(name, &Parser::hasGlobalNamed);
    if (!nearer.empty()) return globalAt(nearer);
    if (const GlobalSym *g = globalAt(name)) return g;
    // Not under the name as written: an unqualified one inside a namespace
    // names that namespace's variable if it has one.
    const std::string key = qualifyForLookup(name, &Parser::hasGlobalNamed);
    return key == name ? nullptr : globalAt(key);
}

// The entry a declaration of this name would redeclare: one an inline namespace's member
// put here under the enclosing name is not it, and the declaration takes the key over.
Parser::GlobalSym *Parser::findGlobalToUpdate(const std::string &name) {
    auto it = globalIndex_.find(name);
    if (it == globalIndex_.end() || globals_[it->second].name != name) return nullptr;
    return &globals_[it->second];
}

const Type *Parser::composite(const Type *a, const Type *b) {
    if (a == b) return a;
    if (!a->isArray() || !b->isArray()) return nullptr;

    const Type *elem = composite(a->pointee(), b->pointee());
    if (elem == nullptr) return nullptr;

    long long la = a->length(), lb = b->length();
    if (la >= 0 && lb >= 0 && la != lb) return nullptr;
    return types_.arrayOf(elem, la >= 0 ? la : lb);
}

ExprPtr Parser::defaultPromote(ExprPtr e) {
    if (e->type()->kind() == Kind::Float)
        return convert(std::move(e), types_.doubleType());
    if (e->type()->isInteger()) {
        const Type *to = promote(e->type());
        return convert(std::move(e), to);
    }
    return e;
}

// cl's other entry points: wmain, WinMain and wWinMain at file scope are C
// names whatever their parameters, as `main` is, and one function each (cl's
// C2731); a member or namespaced one stays C++. No other target has them.
bool Parser::microsoftEntry(const std::string &key) const {
    return target_.microsoftNames() && (key == "wmain" || key == "WinMain" || key == "wWinMain");
}

// The name the linker is given: `main` keeps its own, extern "C" keeps a name with external
// linkage as written ([dcl.link]/4 - an internal one is a C++ name there too, `_ZL2sfi`), and
// everything else is mangled in the target's ABI.
std::string Parser::functionSymbol(const std::string &name, const Type *returns,
                                   const std::vector<const Type *> &params,
                                   bool variadic, bool internal, std::size_t pos) {
    if ((cLinkage_ > 0 && !internal) || name == "main" || (microsoftEntry(name) && !internal)) return name;
    const Type *fn = types_.functionType(returns, params, variadic);
    std::string out, why;
    bool ok = target_.microsoftNames()
            ? microsoftFunctionName(name, manglingType(fn), internal, &out, &why)
            : itaniumFunctionName(name, fn, internal, &out, &why);
    if (!ok)
        src_.fail(pos, "'" + name + "' cannot be given a name the linker can "
                       "hold: " + why);
    return out;
}

// Building an object: the constructor is chosen from the arguments as any overload
// is, then called with the object's address in front of them. The object exists
// before the call - a frame slot like any local - and the constructor fills it.
