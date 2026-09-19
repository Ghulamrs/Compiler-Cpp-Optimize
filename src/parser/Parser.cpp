// The parser: the pieces the rest of it is built on. Reading tokens, looking a
// name up as a type or an enumerator, deciding what is being declared, replaying
// the bodies a class held back, and below those the scopes and symbol tables.

#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>

int alignTo(int n, int a) { return (n + a - 1) / a * a; }

// Recognised by the lexer, with no rule in this parser yet. Naming the keyword is
// the whole point: without this the word reaches expression parsing as an unknown
// identifier and the error lands on whatever follows it.
const char *notYetSupported(const std::string &word) {
    static const char *const pending[] = {
        "asm",
        "char16_t", "char32_t",
        "export",
        "thread_local"
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
    return !e.isXvalue() && isGlvalue(e);
}

const Token &Parser::peekAt(std::size_t n) const {
    std::size_t i = at_ + n;
    return i < tokens_.size() ? tokens_[i] : tokens_.back();
}

bool Parser::consume(const char *s) {
    if (!peek().is(s)) return false;
    at_++;
    return true;
}

void Parser::expect(const char *s) {
    if (!peek().is(s))
        src_.fail(peek().pos, std::string("expected '") + s + "'");
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
    // Written qualified, so there is nothing to search - but `std::size_t` may
    // still be a name a using-declaration put in `std`, and only the alias says
    // what it stands for.
    if (name.find("::") != std::string::npos) {
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

const Type *Parser::findGlobalTypedef(const std::string &name) const {
    auto it = typedefIndex_.find(name);
    return it == typedefIndex_.end() ? nullptr : typedefs_[it->second].type;
}

const Type *Parser::findTypedef(const std::string &name) const {
    // **A class local to this function is found first, and that is what makes it
    // shadow a global of the same name** - [basic.scope.local], and the reason the
    // local scope is a table of its own rather than an entry in the global one.
    auto local = localTypes_.find(name);
    if (local != localTypes_.end()) return local->second;

    auto it = typedefIndex_.find(name);
    if (it != typedefIndex_.end()) return typedefs_[it->second].type;
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
        if (key != name) {
            auto q = typedefIndex_.find(key);
            if (q != typedefIndex_.end()) return typedefs_[q->second].type;
        }
    }

    // **Not found by its own name, so look in the classes this is inside.**
    // Innermost first: a class body being parsed, then the class whose member
    // function's body this is - a nested class is unqualified only from inside.
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
    return nullptr;
}

// A class or enum name is a type name in C++ with no typedef written - inserted
// into the scope the definition appears in, which here is the one table this
// parser has. Not implemented: an object of that name hiding the class name.
void Parser::declareTypeName(const std::string &name, const Type *type) {
    if (findTypedef(name) != nullptr) return;
    typedefIndex_[name] = typedefs_.size();
    typedefs_.push_back(TypedefName{ name, type });
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
    auto it = enumIndex_.find(name);
    if (it != enumIndex_.end()) return &enums_[it->second];

    const std::string key = qualifyForLookup(name, &Parser::hasEnumNamed);
    if (key != name) {
        auto q = enumIndex_.find(key);
        if (q != enumIndex_.end()) return &enums_[q->second];
    }

    return findClassEnum(name);
}

// The class half on its own. An enumerator of the class being parsed is at
// class scope, so it beats a global and loses to a local - which the caller
// in `primary` needs to ask separately from the namespace-scope table above.
const Parser::EnumConst *Parser::findClassEnum(const std::string &name) const {
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
                                     "float", "double",
                                     "struct", "union", "enum",
                                     "const", "volatile",
                                     // It says the next thing is a type, and saying so is the whole of what it does.
                                     "typename",
                                     // And this one *is* a type, written as a
                                     // question about an expression.
                                     "decltype" };
    for (const char *k : t)
        if (peek().is(k)) return true;
    // `::Lexer *p;` declares a p - the leading `::` says where to look and
    // nothing else, so the question is about the name after it.
    if (peek().is("::") && peekAt(1).kind == TokenKind::Ident)
        return findGlobalTypedef(peekAt(1).text) != nullptr;
    if (peek().kind != TokenKind::Ident) return false;
    // `Box<int> b;` declares a variable, so the name has to answer yes here -
    // but only with a `<` after it, since the bare name is not a type.
    auto tmpl = templates_.find(peek().text);
    if (tmpl != templates_.end() && tmpl->second.isClass && peekAt(1).is("<"))
        return true;
    if (findTypedef(peek().text) != nullptr) return true;
    // `N::S` names a type as much as `Outer::Inner` does, and neither answers to
    // its leading name alone. One of those is a namespace and the other a class;
    // past that first token the walk is the same walk.
    return qualifiedTypeEnd() != 0;
}

// How many tokens of an `A::B::C` chain starting here reach a type, or 0 if
// none does. The longest prefix that names one wins, so `Outer::Inner::shared`
// stops at Inner and `N::M::S` runs to the end.
std::size_t Parser::qualifiedTypeEnd() const {
    if (peek().kind != TokenKind::Ident || !peekAt(1).is("::")) return 0;
    std::string q = peek().text;
    std::size_t typeEnd = 0;
    for (std::size_t k = 1; peekAt(k).is("::") &&
                            peekAt(k + 1).kind == TokenKind::Ident;
         k += 2) {
        q += "::" + peekAt(k + 1).text;
        if (findTypedef(q) != nullptr) typeEnd = k + 2;
        // **`std::vector<int> v;` - the name stops at a class template too.**
        if (peekAt(k + 2).is("<") && isClassTemplate(q)) typeEnd = k + 2;
    }
    return typeEnd;
}

std::size_t Parser::qualifiedTypeEndPastArgs() const {
    std::size_t after = qualifiedTypeEnd();
    if (after == 0 || !peekAt(after).is("<")) return after;
    // By depth, counting a `>>` as two - which is how the lexer hands it over.
    int depth = 0;
    for (;; after++) {
        const Token &t = peekAt(after);
        if (t.kind == TokenKind::End) return 0;
        if (t.is("<")) depth++;
        else if (t.is(">")) { if (--depth == 0) return after + 1; }
        else if (t.is(">>")) { depth -= 2; if (depth <= 0) return after + 1; }
    }
}

bool Parser::atDeclarationStart() const {
    // **`Counter::total = 1;` is a statement, not a declaration**, though it opens
    // with a name that names a type: a declaration spelled `C::something` needs a
    // nested class, so a class name before '::' always begins an expression here.
    if (peek().kind == TokenKind::Ident && peekAt(1).is("::")) {
        const Type *named = findTypedef(peek().text);
        // A namespace comes here for the same reason a class does: `N::v = 1;`
        // is a statement and `N::S s;` is a declaration, and only the walk
        // below tells them apart.
        if ((named != nullptr && named->isStructOrUnion()) ||
            namespaces_.find(peek().text) != namespaces_.end()) {
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

    // `constexpr` beside the storage classes, and here rather than in atTypeName:
    // it is a decl-specifier that names no type, so a statement starting with it
    // is a declaration and nothing else - or an expression is asked for instead.
    return atTypeName() || peek().is("static") || peek().is("extern")
        || peek().is("register") || peek().is("auto") || peek().is("typedef")
        || peek().is("constexpr") || peek().is("alignas")
        || (peek().is("[") && peekAt(1).is("["));
}

// From the '{' to the '}' that closes it, counting depth.
void Parser::skipBracedBlock() {
    // A constructor's body may be preceded by its mem-initializer list, so
    // what is skipped starts at the ':' and the '{' is found from there.
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
        else if (peek().is("}")) { depth--; at_++; if (depth == 0) return; continue; }
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
    s.thisOffset = thisOffset_;
    s.currentClass = currentClass_;
    s.returnType = returnType_;
    s.deducingReturn = deducingReturn_;
    s.lambdaCount = lambdaCount_;
    s.atFunctionBody = atFunctionBody_;
    s.alive = alive_;
    s.pendingTemps = pendingTemps_;
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
    s.inNoexceptFunction = inNoexceptFunction_;
    s.replayingInline = replayingInline_;
    s.loopDepth = loopDepth_;
    s.switchDepth = switchDepth_;
    s.switches = switches_;
    s.breakMarks = breakMarks_;
    s.inTryBody = inTryBody_;
    s.inMsHandler = inMsHandler_;
    s.inHandlerBody = inHandlerBody_;
    s.handlerDepth = handlerDepth_;
    s.handlerLoopDepth = handlerLoopDepth_;
    s.handlerSwitchDepth = handlerSwitchDepth_;
    s.handlerFrom = handlerFrom_;
    s.handlerResumeLabel = handlerResumeLabel_;
    s.handlerResumePtr = handlerResumePtr_;
    s.handlerResumeSel = handlerResumeSel_;
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
    thisOffset_ = s.thisOffset;
    currentClass_ = s.currentClass;
    returnType_ = s.returnType;
    deducingReturn_ = s.deducingReturn;
    lambdaCount_ = s.lambdaCount;
    atFunctionBody_ = s.atFunctionBody;
    alive_ = s.alive;
    pendingTemps_ = s.pendingTemps;
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
    inNoexceptFunction_ = s.inNoexceptFunction;
    replayingInline_ = s.replayingInline;
    loopDepth_ = s.loopDepth;
    switchDepth_ = s.switchDepth;
    switches_ = s.switches;
    breakMarks_ = s.breakMarks;
    inTryBody_ = s.inTryBody;
    inMsHandler_ = s.inMsHandler;
    inHandlerBody_ = s.inHandlerBody;
    handlerDepth_ = s.handlerDepth;
    handlerLoopDepth_ = s.handlerLoopDepth;
    handlerSwitchDepth_ = s.handlerSwitchDepth;
    handlerFrom_ = s.handlerFrom;
    handlerResumeLabel_ = s.handlerResumeLabel;
    handlerResumePtr_ = s.handlerResumePtr;
    handlerResumeSel_ = s.handlerResumeSel;
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
    thisOffset_ = 0;
    currentClass_ = nullptr;
    returnType_ = nullptr;
    deducingReturn_ = nullptr;
    lambdaCount_ = 0;
    atFunctionBody_ = false;
    alive_.clear();
    pendingTemps_.clear();
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
    inNoexceptFunction_ = false;
    loopDepth_ = 0;
    switchDepth_ = 0;
    switches_.clear();
    breakMarks_.clear();
    inTryBody_ = false;
    inMsHandler_ = false;
    inHandlerBody_ = false;
    handlerDepth_ = 0;
    handlerLoopDepth_.clear();
    handlerSwitchDepth_.clear();
    handlerFrom_.clear();
    handlerResumeLabel_.clear();
    handlerResumePtr_ = 0;
    handlerResumeSel_ = 0;
    mayThrow_ = 0;
    conditionDecl_ = false;
    conditionName_.clear();
}

void Parser::replayInlineBodies(std::vector<PendingBody> mine) {
    if (mine.empty()) return;
    // **One capture for the whole nested parse.**
    const FunctionState outer = captureFunctionState();
    clearFunctionState();
    // A body replayed here was written inside a class body, so every definition it produces is implicitly inline.
    replayingInline_ = true;

    for (std::size_t i = 0; i < mine.size(); i++) {
        at_ = mine[i].start;
        // A friend's body is written inside the class and belongs outside it,
        // so it is replayed with no owner to supply a `Class::` it must not have.
        inlineOwner_ = mine[i].freeFunction ? std::string() : mine[i].tag;
        inlineOwnerName_ = mine[i].freeFunction ? std::string()
                         : (mine[i].local.empty() ? mine[i].tag : mine[i].local);
        // **A friend is not a member, but it is still looked up in the
        // class.**
        pendingDefaults_.clear();
        pendingNoexcept_ = false;
        const bool scoped = mine[i].freeFunction && !mine[i].tag.empty();
        const Type *scope = scoped ? findTypedef(mine[i].tag) : nullptr;
        if (scope != nullptr) classStack_.push_back(scope);
        topLevel(*current_);
        if (scope != nullptr) classStack_.pop_back();
        inlineOwner_.clear();
        inlineOwnerName_.clear();
    }
    restoreFunctionState(outer);
}

void Parser::enterScope() { scopeStarts_.push_back(locals_.size()); }

int Parser::enterBlock() {
    blocks_.push_back(currentBlock());
    int id = static_cast<int>(blocks_.size()) - 1;
    blockStack_.push_back(id);
    return id;
}

void Parser::leaveBlock() { blockStack_.pop_back(); }

void Parser::leaveScope() {
    locals_.resize(scopeStarts_.back());
    scopeStarts_.pop_back();
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
    return frameSize_;
}

int Parser::declare(const std::string &name, const Type *type, std::size_t pos,
                    int alignAtLeast) {
    refuseWeakAlignas(alignAtLeast, type, pos);
    // A frame slot is only as aligned as the stack it hangs from; past that
    // the frame would have to be realigned, which no backend here does.
    const int typeAlign = type->isReference() ? 0 : type->align(target_);
    if (alignAtLeast > target_.stackAlign() || typeAlign > target_.stackAlign())
        src_.fail(pos, "'alignas(" + std::to_string(alignAtLeast > typeAlign ? alignAtLeast : typeAlign) + ")' on a "
                       "local is not supported yet: the stack is kept " +
                       std::to_string(target_.stackAlign()) + "-aligned on " +
                       target_.name() + ", and a local past that would need "
                       "the frame realigned. A global or a member may ask for "
                       "more");
    if (type->isVoid())
        src_.fail(pos, "'" + name + "' cannot have type void");
    std::size_t from = scopeStarts_.empty() ? 0 : scopeStarts_.back();
    for (std::size_t i = from; i < locals_.size(); i++)
        if (locals_[i].name == name)
            src_.fail(pos, "'" + name + "' is declared twice in this block");

    int offset = allocateFrameSlot(type, alignAtLeast);
    locals_.push_back(Local{ name, offset, type, false, std::string() });
    locals_.back().isParameter = inParams_;
    // The debug record describes the storage, which for a reference is the
    // pointer it really is. DWARF has a tag for a reference and this does not
    // use it yet.
    const Type *stored = type->isReference()
                       ? types_.pointerTo(type->referent()) : type;
    fnVars_.push_back(::Local{ name, stored, offset, inParams_, std::string(),
                              currentBlock() });
    return offset;
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

const Parser::Local *Parser::findLocal(const std::string &name) const {
    for (std::size_t i = locals_.size(); i-- > 0; )
        if (locals_[i].name == name) return &locals_[i];
    return nullptr;
}

const Parser::GlobalSym *Parser::findGlobal(const std::string &name) const {
    auto it = globalIndex_.find(name);
    if (it != globalIndex_.end()) return &globals_[it->second];
    // Not under the name as written: an unqualified one inside a namespace
    // names that namespace's variable if it has one.
    const std::string key = qualifyForLookup(name, &Parser::hasGlobalNamed);
    if (key == name) return nullptr;
    it = globalIndex_.find(key);
    return it == globalIndex_.end() ? nullptr : &globals_[it->second];
}

Parser::GlobalSym *Parser::findGlobalToUpdate(const std::string &name) {
    auto it = globalIndex_.find(name);
    return it == globalIndex_.end() ? nullptr : &globals_[it->second];
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

// The name the linker is given: `main` keeps its own by the rule that makes it
// findable at all, anything inside extern "C" because that is what the linkage
// specification asked for, and everything else is mangled in the target's ABI.
std::string Parser::functionSymbol(const std::string &name, const Type *returns,
                                   const std::vector<const Type *> &params,
                                   bool variadic, bool internal, std::size_t pos) {
    if (cLinkage_ > 0 || name == "main") return name;
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
