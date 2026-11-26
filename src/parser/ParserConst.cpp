// The parser: what is evaluated where it is written.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Mangle.h"
#include "../Source.h"

#include <climits>
#include <cstring>

// [dcl.typedef]/2: `using X = T;` names T as `typedef T X;` would, in whichever scope it stands -
// `prefix` is what the key takes there (a namespace's, a class's tag and "::", or nothing in a block).
bool Parser::aliasDeclaration(const std::string &prefix) {
    if (!peek().is("using")) return false;
    if (peekAt(1).kind != TokenKind::Ident || !(peekAt(2).is("=") || peekAt(2).is("["))) return false;
    at_++;
    const std::size_t pos = peek().pos;
    const std::string name = peek().text;
    at_++;
    skipAttributes();
    expect("=");
    const std::size_t typeAt = peek().pos;
    StorageClass sc;
    const Type *t = specifiers(&sc);
    if (sc != StorageNone)
        src_.fail(typeAt, "an alias declaration names a type, and a storage class is not part of one");
    if (t->unqualified()->kind() == Kind::Deduced)
        src_.fail(typeAt, "an alias declaration names a type, and 'auto' deduces one from an "
                          "initialiser it does not have");
    // `using F = int(int);` - a function type, whose parameter list no declarator reads;
    // `using PF = int (S::*)() const;` a pointer to a member function, which one does.
    Declared d{ "", t, typeAt, 0, "" };
    if (peek().is("(") && !atParenDeclarator() && !atParenInitialiser()) typedefFunctionSuffix(d);
    else d = declarator(t, true);
    if (!d.name.empty())
        src_.fail(d.pos, "an alias declaration names a type, and '" + d.name + "' would declare something "
                         "of that type - write 'using " + name + " = T;' with T a type alone");
    expect(";");
    const std::string key = prefix + name;
    std::unordered_map<std::string, std::size_t>::const_iterator had = typedefIndex_.find(key);
    if (had != typedefIndex_.end() && typedefs_[had->second].type != d.type &&
        typedefs_[had->second].name == key)
        src_.fail(pos, "'" + name + "' is declared twice, and not as the same type: it was '" +
                       typedefs_[had->second].type->describe() + "' and is now '" + d.type->describe() + "'");
    typedefIndex_[key] = typedefs_.size();
    typedefs_.push_back(TypedefName{ key, d.type });
    return true;
}

// [temp.explicit]/2: `extern template` promises a definition elsewhere, and every specialization here is
// mergeable (weak, COMDAT), so the one it would suppress folds with that one - read and dropped.
bool Parser::externTemplateDeclaration() {
    if (!peek().is("extern") || !peekAt(1).is("template")) return false;
    const std::size_t pos = peek().pos;
    at_ += 2;
    int depth = 0;
    for (;; at_++) {
        if (peek().kind == TokenKind::End) src_.fail(pos, "this 'extern template' declaration never ends");
        if (peek().is("(") || peek().is("[") || peek().is("{")) depth++;
        else if (peek().is(")") || peek().is("]") || peek().is("}")) depth--;
        else if (depth == 0 && peek().is(";")) break;
    }
    at_++;
    return true;
}

// **`= default` and `= delete` sit exactly where `= 0` does**, after the virt-specifiers, and every
// declare* that follows reads which one was written. Outside a class `= default` defines a special
// member the class declared, and the out-of-line definition reads it.
void Parser::readDefaultedOrDeleted() {
    pendingSpecial_ = SpecialDef::None;
    if (!peek().is("=")) return;
    const bool def = peekAt(1).is("default");
    if (!def && !peekAt(1).is("delete")) return;
    pendingSpecialAt_ = peek().pos;
    pendingSpecial_ = def ? SpecialDef::Defaulted : SpecialDef::Deleted;
    at_ += 2;
}

// [dcl.fct.def.delete]/2: a deleted function may not be used, and this is said where the use is.
void Parser::refuseDeleted(const Signature &f, std::size_t pos) const {
    if (!f.deleted) return;
    const std::string name = f.owner.empty() ? f.name : f.owner + "::" + f.name;
    if (f.implicit)
        src_.fail(pos, "'" + name + "' is deleted - the compiler would write it, and [class.copy] deletes "
                       "it: a move the class declares, or a base or member whose own is deleted");
    src_.fail(pos, "'" + name + "' is deleted - it was declared '= delete', and a deleted function "
                   "cannot be called, copied from or taken the address of");
}

// [dcl.fct.def.default]/1: only a special member may be defaulted, and once per class.
void Parser::defaultedMember(DefaultedMember &m, Access access, std::size_t pos, const std::string &what) {
    if (m.is) src_.fail(pos, what + " is defaulted twice");
    m.is = true;
    m.access = access;
    m.quiet = pendingNoexcept_;
    pendingNoexcept_ = false;
}

// What the class wrote `= default` on, or nothing.
Parser::Defaulted Parser::defaultedFor(const std::string &tag) const {
    std::map<std::string, Defaulted>::const_iterator it = defaulted_.find(tag);
    return it != defaulted_.end() ? it->second : Defaulted();
}

// [dcl.constexpr]/4: a C++11 constexpr constructor's body is empty; statements in it are C++14.
void Parser::refuseConstexprConstructorBody(std::size_t parenAt) const {
    std::size_t k = parenAt;
    int depth = 0;
    for (;; k++) {
        const Token &t = peekAt(k);
        if (t.kind == TokenKind::End || (depth == 0 && t.is(";"))) return;
        if (t.is("(") || t.is("[")) depth++;
        else if (t.is(")") || t.is("]")) depth--;
        else if (t.is("{")) {
            const Token &before = peekAt(k - 1);
            if (depth == 0 && before.kind != TokenKind::Ident && !before.is(">")) break;
            depth++;
        } else if (t.is("}")) depth--;
    }
    if (peekAt(k + 1).is("}")) return;
    src_.fail(peekAt(k + 1).pos, "a 'constexpr' constructor with statements in its body is C++14, "
                                 "and this compiler is C++11 - [dcl.constexpr]/4 asks for an empty "
                                 "body and the work in the initialiser list");
}


// `friend class X;`, or `friend X;` for a class already declared - [class.friend]/2: every member
// of X reaches what `owner` keeps private. An X not yet declared is declared by it in the namespace
// around `owner`, [namespace.memdef]/3.
void Parser::befriendClass(const std::string &owner, bool keyed) {
    if (keyed) at_++;
    const std::string name = expectIdent("the class to befriend");
    std::string key = name;
    // [class.friend]/3: `friend class valarray<T>;` - one specialization, named by its template-id.
    if (peek().is("<") && isClassTemplate(name)) {
        at_--;
        StorageClass sc;
        const Type *t = specifiers(&sc);
        friendClasses_[owner].push_back(t->unqualified()->tag());
        expect(";");
        return;
    }
    if (const Type *t = findTypedef(name)) key = t->unqualified()->tag();
    else if (!namespaceStack_.empty()) key = namespacePrefix() + name;
    friendClasses_[owner].push_back(key);
    expect(";");
}

// Is the code being read inside a member of a class `cls` befriended - or of a class nested in one?
bool Parser::isFriendClassOf(const Type *cls) const {
    if (cls == nullptr) return false;
    std::map<std::string, std::vector<std::string> >::const_iterator it =
        friendClasses_.find(cls->unqualified()->tag());
    if (it == friendClasses_.end()) return false;
    const Type *here[3] = { currentClass_, classStack_.empty() ? nullptr : classStack_.back(), lambdaScope() };
    for (const Type *h : here)
        for (const Type *c = h; c != nullptr; c = c->enclosing())
            for (std::size_t i = 0; i < it->second.size(); i++) {
                if (c->unqualified()->tag() == it->second[i]) return true;
                // `template <class U> friend class H;` befriends every specialization of H (R12).
                if (c->unqualified()->isSpecialization() &&
                    it->second[i] == "template " + c->unqualified()->templateName())
                    return true;
            }
    return false;
}

// The implicit member a `= default` stands for was declared public; it takes the access it was written under,
// and `noexcept` where it was written with it.
void Parser::applyDefaultedAccess(const std::string &tag, const Type *type) {
    std::map<std::string, Defaulted>::const_iterator it = defaulted_.find(tag);
    if (it == defaulted_.end()) return;
    const Defaulted &df = it->second;
    auto apply = [this](const Signature *f, const DefaultedMember &m) {
        if (f == nullptr || !m.is) return;
        Signature &made = functions_[static_cast<std::size_t>(f - &functions_[0])];
        made.access = m.access;
        if (m.quiet) made.isNoexcept = true;
    };
    apply(defaultConstructorOf(type), df.defaultCtor);
    apply(copyConstructorOf(type), df.copyCtor);
    apply(moveConstructorOf(type), df.moveCtor);
    apply(copyAssignOf(type), df.copyAssign);
    apply(destructorOf(type), df.dtor);
}

bool Parser::staticAssertion() {
    if (!peek().is("static_assert")) return false;
    const std::size_t pos = peek().pos;
    at_++;
    expect("(");

    const std::size_t condAt = peek().pos;
    ExprPtr cond = decay(conditional());
    long long value;
    if (!fold(*cond, &value, condAt))
        src_.fail(condAt, "the condition of a 'static_assert' has to be a "
                          "constant the compiler can work out here, and this "
                          "is not one");

    if (peek().is(")"))
        src_.fail(peek().pos, "a 'static_assert' with no message is C++17 - "
                              "write the message this one would have printed, "
                              "'static_assert(cond, \"why\")'");
    expect(",");

    if (peek().kind != TokenKind::Str)
        src_.fail(peek().pos, "the message of a 'static_assert' has to be "
                              "written out as a string literal - it is printed "
                              "by the compiler, so there is no program running "
                              "to read a variable");
    std::string message = peek().text;
    at_++;
    while (peek().kind == TokenKind::Str) {   // "a" "b" is one literal
        message += peek().text;
        at_++;
    }

    expect(")");
    expect(";");

    if (value == 0) src_.fail(pos, "static assertion failed: " + message);
    return true;
}

// **The exception specification, and the one thing it does not touch.** In C++11 it is
// *not* part of the function's type - measured, both spellings mangle alike - so what
// it buys is `noexcept(e)`, or `throw(T...)`'s list, kept in pendingSpec_ for the definition.
// Whether the operand of `noexcept(`, from token `from`, holds an unevaluated operand of its own - the only
// place a constant expression can name a function parameter.
bool Parser::noexceptOperandMayNameParameters(std::size_t from) const {
    int depth = 0;
    for (std::size_t i = from; i < tokens_.size() && tokens_[i].kind != TokenKind::End; i++) {
        if (tokens_[i].is("(")) depth++;
        else if (tokens_[i].is(")")) { if (depth == 0) break; depth--; }
        else if (tokens_[i].is("noexcept") || tokens_[i].is("sizeof") || tokens_[i].is("decltype")) return true;
    }
    return false;
}

// The function just declared takes the specification that waits for its class, if one does.
void Parser::keepDeferredSpec() {
    if (!specDeferred_) return;
    specDeferred_ = false;
    if (functions_.empty() || classStack_.empty()) return;
    DeferredSpec d;
    d.cls = classStack_.back();
    d.function = functions_.size() - 1;
    d.operandAt = specOperandAt_;
    d.paramsOpen = specParamsOpen_;
    deferredSpecs_.push_back(d);
}

// The class is complete: each of its kept specifications is read where it was written, its members in scope.
// One may ask another's - `noexcept(noexcept(T().touch()))` of a constructor and a member declared below - so
// they are read again until a whole round changes nothing; what an early reading answered is only a start.
void Parser::settleDeferredSpecs(const Type *cls) {
    std::vector<DeferredSpec> mine;
    for (std::size_t k = 0; k < deferredSpecs_.size(); ) {
        if (deferredSpecs_[k].cls != cls) { k++; continue; }
        mine.push_back(deferredSpecs_[k]);
        deferredSpecs_.erase(deferredSpecs_.begin() + static_cast<std::ptrdiff_t>(k));
    }
    if (mine.empty()) return;
    const std::size_t resume = at_;
    // (currentClass_ may still name the class of the last member defined outside its class)
    const Type *wasClass = currentClass_;
    currentClass_ = cls;
    for (std::size_t round = 0; round <= mine.size(); round++) {
        bool changed = false;
        for (const DeferredSpec &d : mine) {
            if (d.function >= functions_.size()) continue;
            at_ = d.operandAt;
            if (d.paramsOpen != 0) { declareParametersAt(d.paramsOpen); at_ = d.operandAt; }
            const long long value = constantExpression("a constant in 'noexcept('");
            if (d.paramsOpen != 0) {
                locals_.resize(scopeStarts_.back());
                scopeStarts_.pop_back();
            }
            expect(")");
            if (functions_[d.function].isNoexcept != (value != 0)) changed = true;
            functions_[d.function].isNoexcept = value != 0;
            functions_[d.function].spec.allowsAll = value == 0;
        }
        if (!changed) break;
    }
    at_ = resume;
    currentClass_ = wasClass;
    // [except.spec]/5, which the declaration left for here: an overrider promises no less than what it overrides.
    for (const DeferredSpec &d : mine)
        if (d.function < functions_.size() && functions_[d.function].isVirtual)
            checkOverride(cls, functions_[d.function], functions_[d.function].deleted, tokens_[d.operandAt].pos);
}

bool Parser::exceptionSpecification() {
    skipAttributes();
    pendingSpec_ = DynamicSpec();
    specDeferred_ = false;
    bool noexc = false;
    if (consume("noexcept")) {
        noexc = true;
        if (consume("(")) {
            // [basic.scope.block]/2: the function's parameters are in scope here, and an unevaluated operand
            // may name them - `noexcept(noexcept(x.swap(y)))`, the standard's own spelling (1.77: they were
            // not declared yet). Found from the list this follows, past the cv- and ref-qualifiers, and
            // declared only where an operand that can name one is written.
            std::size_t open = 0;
            if (noexceptOperandMayNameParameters(at_)) {
                std::size_t i = at_ >= 3 ? at_ - 3 : 0;      // the token before `noexcept (`
                while (i > 0 && (tokens_[i].is("const") || tokens_[i].is("volatile") || tokens_[i].is("&") ||
                                 tokens_[i].is("&&")))
                    i--;
                if (i > 0 && tokens_[i].is(")")) {
                    int nest = 0;
                    for (;; i--) {
                        if (tokens_[i].is(")")) nest++;
                        else if (tokens_[i].is("(") && --nest == 0) { open = i; break; }
                        if (i == 0) break;
                    }
                }
            }
            const std::size_t operandAt = at_;
            // CWG 1330: in a class still being defined the operand may fail for a member declared below it;
            // then it waits for the class (keepDeferredSpec, settleDeferredSpecs).
            const bool mayWait = !classStack_.empty() && !classStack_.back()->isComplete() && !patternOnly_ &&
                                 !src_.inTrial();
            const std::size_t localsWere = locals_.size(), scopesWere = scopeStarts_.size();
            long long value = 0;
            bool waits = false;
            if (mayWait) {
                std::size_t after = 0;
                try {
                    Trial trial(this);
                    if (open != 0) { declareParametersAt(open); at_ = operandAt; }
                    value = constantExpression("a constant in 'noexcept('");
                    after = at_;
                } catch (const SubstitutionFailure &f) {
                    if (f.unsupported) src_.fail(f.pos, f.why);
                    waits = true;
                }
                locals_.resize(localsWere);
                scopeStarts_.resize(scopesWere);
                if (waits) {
                    // to the `)` that closes `noexcept(`
                    at_ = operandAt;
                    for (int depth = 1; peek().kind != TokenKind::End; at_++) {
                        if (peek().is("(") || peek().is("[") || peek().is("{")) depth++;
                        else if ((peek().is(")") || peek().is("]") || peek().is("}")) && --depth == 0) break;
                    }
                    value = 0;
                } else {
                    at_ = after;
                }
                // An operand that names anything is read again with the class complete, whatever it
                // answered here: a member declared below may change what a call in it finds.
                bool names = waits;
                for (std::size_t i = operandAt; i < at_ && !names; i++) names = tokens_[i].kind == TokenKind::Ident;
                if (names) {
                    specDeferred_ = true;
                    specOperandAt_ = operandAt;
                    specParamsOpen_ = open;
                }
            } else {
                if (open != 0) { declareParametersAt(open); at_ = operandAt; }
                value = constantExpression("a constant in 'noexcept('");
                if (open != 0) {
                    locals_.resize(scopeStarts_.back());
                    scopeStarts_.pop_back();
                }
            }
            expect(")");
            noexc = value != 0;
            pendingSpec_.allowsAll = !noexc;
        }
    } else if (peek().is("throw") && peekAt(1).is("(")) {
        const std::size_t at = peek().pos;
        at_ += 2;
        if (consume(")")) {
            noexc = true;
        } else {
            pendingSpec_.written = true;
            pendingSpec_.pos = at;
            do {
                const std::size_t tpos = peek().pos;
                // [temp.variadic]/5: `throw(Ts)` names the pack without the '...' that expands it.
                const std::string named = peek().kind == TokenKind::Ident ? peek().text : std::string();
                StorageClass sc;
                const Type *t = declarator(specifiers(&sc), true).type;
                if (!named.empty() && packs_.count(named) != 0 && !peek().is("..."))
                    src_.fail(tpos, "'" + named + "' is a parameter pack, and here it is written without the "
                                    "'...' that expands it");
                if (peek().is("..."))
                    src_.fail(peek().pos, "a pack expansion in a dynamic exception specification is not "
                                          "supported yet - write the types out");
                pendingSpec_.types.push_back(adjustedSpecType(t, tpos));
            } while (consume(","));
            expect(")");
        }
    }
    readVirtSpecifiers();
    readDefaultedOrDeleted();
    return noexc;
}

// [except.spec]/2: a reference names its referent, an array or a function decays, and the
// top-level qualifiers go - what is left must be complete, or point at a complete type or void.
const Type *Parser::adjustedSpecType(const Type *t, std::size_t pos) {
    if (t->isRValueReference())
        src_.fail(pos, "an rvalue reference type is not allowed in an exception specification - [except.spec]/2");
    if (t->isReference()) t = t->referent();
    if (t->isArray()) t = types_.pointerTo(t->pointee());
    else if (t->isFunction()) t = types_.pointerTo(t);
    t = t->unqualified();
    if (patternOnly_) return t;
    const Type *named = (t->isPointer() ? t->pointee() : t)->unqualified();
    if (named->kind() == Kind::TemplateParam || named->kind() == Kind::DependentMember) return t;
    if (!named->isVoid() && !named->isComplete())
        src_.fail(pos, "'" + named->describe() + "' is incomplete, and an exception specification may "
                       "only name a complete type, or a pointer or reference to one - [except.spec]/2");
    return t;
}

// **`override` and `final` are contextual and follow the exception specification** - [class.mem]. Inside a
// class body they are recorded for the declaration being read; a held body's replay has already checked
// them and steps over; anywhere else - a free function, an out-of-line definition - they are ill-formed.
void Parser::readVirtSpecifiers() {
    pendingVirt_ = VirtSpecifiers();
    while (peek().is("override") || peek().is("final")) {
        const bool ov = peek().is("override");
        VirtSpecifiers &v = pendingVirt_;
        if ((ov && v.isOverride) || (!ov && v.isFinal))
            src_.fail(peek().pos, std::string("'") + peek().text + "' is written twice");
        if (ov) { v.isOverride = true; v.overrideAt = peek().pos; }
        else    { v.isFinal = true;    v.finalAt = peek().pos; }
        if (classStack_.empty() && !replayingInline_)
            src_.fail(peek().pos, std::string("'") + peek().text + "' can only be written on a "
                                  "member function's declaration inside its class - "
                                  "[class.mem] - and this declaration is outside one");
        at_++;
    }
    if (!classStack_.empty()) return;
    pendingVirt_ = VirtSpecifiers();
}

// A constructor, a static member or a data member may carry neither word.
void Parser::refuseVirtSpecifiers(const char *what) {
    const VirtSpecifiers v = takeVirtSpecifiers();
    if (v.isOverride)
        src_.fail(v.overrideAt, std::string("'override' marks a virtual member function, and ") + what +
                                " cannot be virtual");
    if (v.isFinal)
        src_.fail(v.finalAt, std::string("'final' marks a virtual member function, and ") + what +
                             " cannot be virtual");
}

long long Parser::constantExpression(const char *what) {
    std::size_t pos = peek().pos;
    ExprPtr e = decay(conditional());
    // [expr.const]/5: a literal class where an integral constant is wanted is converted by its one
    // non-explicit conversion function to a number - `int a[t];` for a constexpr object t.
    if (e->type() != nullptr && e->type()->unqualified()->isStructOrUnion())
        if (const Signature *how = soleNumericConversion(e->type())) {
            const Type *object = e->type();
            e = memberCallWith(std::move(e), object, how->name, pos, std::vector<ExprPtr>());
        }
    long long v;
    if (!fold(*e, &v, pos))
        src_.fail(pos, std::string("expected ") + what +
                       ", and this is not an integer constant expression");
    return v;
}

// The frame an object fold outside any call stands in, so a temporary it builds has a slot to live in.
static const int kScratchFrame = INT_MIN;

const Parser::ConstexprSlot *Parser::constexprSlot(const Var &v) const {
    if (constexprFrames_.empty()) return nullptr;
    const std::vector<ConstexprSlot> &frame = constexprFrames_.back();
    for (std::size_t i = 0; i < frame.size(); i++) {
        if (frame[i].offset == kScratchFrame) continue;
        if (v.isLocal() ? frame[i].global.empty() && frame[i].offset == v.offset()
                        : frame[i].global == v.symbol() || frame[i].global == v.name()) return &frame[i];
    }
    return nullptr;
}

bool Parser::enterConstexprCall(const Call &c, const ConstexprFn **out,
                                std::size_t pos) const {
    std::string symbol = c.symbol();
    // **A call through a pointer that names one function** - `F(x)` with F a template argument `&sq` - is a
    // call of that function ([expr.const]/2; 1.82: `apply<sq>(3)` did not fold).
    if (c.callee() != nullptr) {
        const Expr *e = c.callee();
        for (;;) {
            if (const Cast *k = dynamic_cast<const Cast *>(e)) { e = &k->value(); continue; }
            if (const Unary *u = dynamic_cast<const Unary *>(e))
                if (u->op() == '&' || u->op() == '*') { e = &u->operand(); continue; }
            break;
        }
        const Var *v = dynamic_cast<const Var *>(e);
        if (v == nullptr || v->isLocal() || v->type() == nullptr || !v->type()->isFunction()) return false;
        symbol = v->symbol();
    }
    auto it = constexprFns_.find(symbol);
    if (it == constexprFns_.end() && instantiateForConstant(symbol)) it = constexprFns_.find(symbol);
    if (it == constexprFns_.end()) return false;
    const ConstexprFn &fn = it->second;
    if (fn.value == nullptr || c.args().size() != fn.slots.size())
        return false;

    // A recursion that does not end is a compiler that does not either.
    // The standard lets an implementation set a limit and say so; this is
    // that limit - twice Annex B's 512 - and it is said where it is reached.
    if (constexprFrames_.size() >= kMaxConstexprCalls)
        src_.fail(pos, "this constant expression is more than " + std::to_string(kMaxConstexprCalls) + " calls "
                       "deep - a 'constexpr' function that never stops "
                           "recursing cannot be worked out while compiling");

    std::vector<ConstexprSlot> frame;
    for (std::size_t i = 0; i < c.args().size(); i++) {
        ConstexprSlot slot;
        slot.offset = fn.slots[i];
        const Expr &a = *c.args()[i];
        // **Folded outside the new frame, in the caller's.** An argument is an
        // expression where the call is written, so `fact(n - 1)` reads the
        // caller's n; folding it after the push would read the callee's slot.
        if (!foldArgument(a, &slot, pos)) return false;
        frame.push_back(slot);
    }
    constexprFrames_.push_back(frame);
    *out = &fn;
    return true;
}

// **An address built on a constant pointer is a constant**, `(size_t)&((S *)0)->m` - offsetof,
// [support.types]/4: a cast of an integer to a pointer, `*` of it, members of that, and `&`.
bool Parser::foldPointerConstant(const Expr &e, long long *out, std::size_t pos) const {
    if (const Cast *c = dynamic_cast<const Cast *>(&e)) {
        if (c->value().type() != nullptr && c->value().type()->isPointer())
            return foldPointerConstant(c->value(), out, pos);
        return c->value().type() != nullptr && c->value().type()->isInteger() && fold(c->value(), out, pos);
    }
    const Unary *u = dynamic_cast<const Unary *>(&e);
    if (u == nullptr || u->op() != '&') return false;
    const Expr *at = &u->operand();
    long long offset = 0;
    while (const MemberAccess *m = dynamic_cast<const MemberAccess *>(at)) {
        if (m->isBitField()) return false;
        offset += m->offset();
        at = &m->object();
    }
    const Unary *deref = dynamic_cast<const Unary *>(at);
    if (deref == nullptr || deref->op() != '*' || !foldPointerConstant(deref->operand(), out, pos))
        return false;
    *out += offset;
    return true;
}

bool Parser::fold(const Expr &e, long long *out, std::size_t pos) const {
    // **A name, when it names a constant.**
    if (const Var *v = dynamic_cast<const Var *>(&e)) {
        // **Inside a constexpr call, a local name is a parameter.** The body being
        // folded belongs to another function, so its Vars name slots in a frame that
        // does not exist; what they are worth is on the top of this stack.
        if (const ConstexprSlot *slot = constexprSlot(*v)) {
            if (slot->floating || slot->obj != nullptr || slot->pointer) return false;   // Cast/Binary fold it as floating
            *out = slot->i;
            return true;
        }
        if (v->isLocal()) {
            if (const Local *l = findLocal(v->name()))
                if (l->isConstantValue) { *out = l->constantValue; return true; }
            return false;
        }
        if (const GlobalSym *g = findGlobal(v->name()))
            if (g->isConstantValue) { *out = g->constantValue; return true; }
        // A static member is not in globals_ - it is kept apart from the
        // member list so nothing walking a layout has to skip it - so the
        // read-back asks the table keyed by its symbol.
        if (const StaticConst *sc = findStaticConst(v->symbol()))
            if (sc->known) { *out = sc->value; return true; }
        return false;
    }
    // **A call to a constexpr function.** C++11 lets its body be one return statement,
    // so running it is folding that expression with the parameters standing for the
    // arguments. A call to anything else simply does not fold, which is the answer.
    if (const Call *c = dynamic_cast<const Call *>(&e)) {
        const ConstexprFn *fn = nullptr;
        if (!enterConstexprCall(*c, &fn, pos)) return false;
        long long result = 0;
        const bool ok = fold(*fn->value, &result, pos);
        constexprFrames_.pop_back();
        if (!ok) return false;
        *out = result;
        return true;
    }
    if (const Num *n = dynamic_cast<const Num *>(&e)) {
        if (n->type()->isFloating()) return false;
        *out = n->value();
        return true;
    }
    // [expr.comma]: the right operand is the value; the left is a constant too or an effect a
    // constexpr body is allowed, so `int a[(1, 3)]` has 3 elements (1.27, 2026-10-21, review of
    // 1.25, item 3 - it was "not an integer constant expression").
    if (const Comma *c = dynamic_cast<const Comma *>(&e)) {
        long long dropped = 0;
        if (!fold(c->left(), &dropped, pos) && !runConstexprEffect(c->left(), pos)) return false;
        return fold(c->right(), out, pos);
    }
    if (const Expr *chosen = chosenLvalue(e, pos)) return fold(*chosen, out, pos);
    // **A member of a constant object**, or what a pointer into one reaches.
    if (dynamic_cast<const MemberAccess *>(&e) != nullptr ||
        (dynamic_cast<const Unary *>(&e) != nullptr && static_cast<const Unary &>(e).op() == '*')) {
        ConstLeaf leaf;
        if (!foldLeaf(e, &leaf, pos) || leaf.type->isFloating()) return false;
        *out = leaf.i;
        return true;
    }

    // **[expr.const] has a floating lane, and this fold answers in integers**,
    // so a floating operand goes to foldDouble where an integer comes out of
    // it - a cast, a comparison, `!` - and a floating result stays its own.
    if (const Cast *c = dynamic_cast<const Cast *>(&e)) {
        long long v;
        if (!e.type()->isInteger()) return false;
        if (c->value().type() != nullptr && c->value().type()->isFloating()) {
            long double d = 0;
            if (!foldFloating(c->value(), &d)) return false;
            v = e.type()->isSigned(target_)
              ? static_cast<long long>(d)
              : static_cast<long long>(static_cast<unsigned long long>(d));
        } else if (c->value().type() != nullptr && c->value().type()->isPointer()) {
            if (!foldPointerConstant(c->value(), &v, pos)) return false;
        } else if (!fold(c->value(), &v, pos)) {
            return false;
        }
        *out = narrowTo(v, e.type());
        return true;
    }

    if (const Unary *u = dynamic_cast<const Unary *>(&e)) {
        long long v;
        if (u->op() == '!' && u->operand().type() != nullptr &&
            u->operand().type()->isFloating()) {
            long double d = 0;
            if (!foldFloating(u->operand(), &d)) return false;
            *out = d == 0;
            return true;
        }
        if (!fold(u->operand(), &v, pos)) return false;
        switch (u->op()) {
        case '-': *out = static_cast<long long>(0ULL - static_cast<unsigned long long>(v)); return true;
        case '+': *out = v; return true;
        case '!': *out = !v; return true;
        case '~': *out = ~v; return true;
        default: return false;
        }
    }

    if (const Conditional *c = dynamic_cast<const Conditional *>(&e)) {
        long long t;
        if (!fold(c->cond(), &t, pos)) return false;
        return fold(t ? c->thenArm() : c->elseArm(), out, pos);
    }

    if (const Binary *b = dynamic_cast<const Binary *>(&e)) {
        long long l, r;
        const Type *lt = b->lhs().type();
        const Type *rt = b->rhs().type();
        // [expr.log.and]/1, [expr.log.or]/1: a left operand that decides leaves the right one unevaluated, so it
        // need not be constant - `B >= 0 && A <= INTMAX_MAX - B` for a negative B.
        if (b->op() == BinOp::LAnd || b->op() == BinOp::LOr) {
            bool sides[2];
            const Expr *operands[2] = { &b->lhs(), &b->rhs() };
            for (int k = 0; k < 2; k++) {
                const Type *ot = operands[k]->type();
                if (ot != nullptr && ot->isFloating()) {
                    long double d = 0;
                    if (!foldFloating(*operands[k], &d)) return false;
                    sides[k] = d != 0;
                } else {
                    long long v;
                    if (!fold(*operands[k], &v, pos)) return false;
                    sides[k] = v != 0;
                }
                if (k == 0 && sides[0] == (b->op() == BinOp::LOr)) { *out = sides[0]; return true; }
            }
            *out = sides[1];
            return true;
        }
        if ((lt != nullptr && lt->isFloating()) ||
            (rt != nullptr && rt->isFloating())) {
            long double dl = 0, dr = 0;
            if (!foldFloating(b->lhs(), &dl) || !foldFloating(b->rhs(), &dr))
                return false;
            switch (b->op()) {
            case BinOp::Eq: *out = (dl == dr); return true;
            case BinOp::Ne: *out = (dl != dr); return true;
            case BinOp::Lt: *out = (dl <  dr); return true;
            case BinOp::Le: *out = (dl <= dr); return true;
            case BinOp::Gt: *out = (dl >  dr); return true;
            case BinOp::Ge: *out = (dl >= dr); return true;
            case BinOp::LAnd: *out = (dl != 0 && dr != 0); return true;
            case BinOp::LOr:  *out = (dl != 0 || dr != 0); return true;
            default: return false;
            }
        }
        if (!fold(b->lhs(), &l, pos) || !fold(b->rhs(), &r, pos)) return false;

        const Type *t = b->lhs().type();
        bool uns = t->isInteger() && !t->isSigned(target_);
        unsigned long long ul = static_cast<unsigned long long>(l);
        unsigned long long ur = static_cast<unsigned long long>(r);

        // [expr.const]/2: an operation whose result is undefined - a signed one outside its type, a
        // shift past the promoted operand's width - is no constant expression, and is not folded.
        const Type *res = e.type();
        const int bits = res != nullptr && res->isInteger() ? res->size(target_) * 8 : 64;
        const bool signedResult = res != nullptr && res->isInteger() && res->isSigned(target_);
        auto fits = [&](long long v) {
            return !signedResult || bits >= 64 ||
                   (v >= -(1LL << (bits - 1)) && v <= (1LL << (bits - 1)) - 1);
        };
        if (signedResult) {
            if (b->op() == BinOp::Add && (bits >= 64 ? ((r > 0 && l > LLONG_MAX - r) || (r < 0 && l < LLONG_MIN - r))
                                                     : !fits(l + r)))
                return false;
            if (b->op() == BinOp::Sub && (bits >= 64 ? ((r < 0 && l > LLONG_MAX + r) || (r > 0 && l < LLONG_MIN + r))
                                                     : !fits(l - r)))
                return false;
            if (b->op() == BinOp::Mul && (bits >= 64 ? (l != 0 && r != 0 &&
                                                        ((l == -1 && r == LLONG_MIN) || (r == -1 && l == LLONG_MIN) ||
                                                         (l != -1 && r != -1 && (l * r) / r != l)))
                                                     : !fits(l * r)))
                return false;
        }
        if ((b->op() == BinOp::Shl || b->op() == BinOp::Shr) && res != nullptr && res->isInteger() &&
            r >= 0 && r < 64 && r >= bits)
            return false;
        // A non-negative signed operand shifts into the sign bit by CWG 1457, which clang applies to C++11.
        if (b->op() == BinOp::Shl && signedResult && r >= 0 && r < bits &&
            (l < 0 || (bits >= 64 ? ul > (~0ULL >> r) : (ul << r) >= (1ULL << bits))))
            return false;

        switch (b->op()) {
        case BinOp::Add: *out = static_cast<long long>(ul + ur); return true;
        case BinOp::Sub: *out = static_cast<long long>(ul - ur); return true;
        case BinOp::Mul: *out = static_cast<long long>(ul * ur); return true;
        case BinOp::Div:
        case BinOp::Mod:
            if (r == 0)
                src_.fail(pos, "division by zero in a constant expression");
            if (!uns && ul == (1ULL << 63) && r == -1) {
                *out = (b->op() == BinOp::Div) ? l : 0;
                return true;
            }
            if (b->op() == BinOp::Div)
                *out = uns ? static_cast<long long>(ul / ur) : l / r;
            else
                *out = uns ? static_cast<long long>(ul % ur) : l % r;
            return true;
        case BinOp::Shl:
        case BinOp::Shr:
            if (r < 0 || r >= 64)
                src_.fail(pos, "shift count out of range in a constant expression");
            if (b->op() == BinOp::Shl) *out = static_cast<long long>(ul << r);
            else *out = uns ? static_cast<long long>(ul >> r) : (l >> r);
            return true;
        case BinOp::BitAnd: *out = l & r; return true;
        case BinOp::BitOr:  *out = l | r; return true;
        case BinOp::BitXor: *out = l ^ r; return true;
        case BinOp::Eq: *out = (l == r); return true;
        case BinOp::Ne: *out = (l != r); return true;
        case BinOp::Lt: *out = uns ? (ul <  ur) : (l <  r); return true;
        case BinOp::Le: *out = uns ? (ul <= ur) : (l <= r); return true;
        case BinOp::Gt: *out = uns ? (ul >  ur) : (l >  r); return true;
        case BinOp::Ge: *out = uns ? (ul >= ur) : (l >= r); return true;
        case BinOp::LAnd: *out = (l && r); return true;
        case BinOp::LOr:  *out = (l || r); return true;
        }
        return false;
    }

    return false;
}

// **[expr.const]/3: a named integral constant is a constant expression**, which is the
// rule that lets C++ write `const int n = 4; int a[n];`. Only integral, because that
// is what fold() answers in. Below it: the one expression a constexpr body may be.
const Expr *Parser::singleReturnValue(const Stmt &body) const {
    const Stmt *at = &body;
    for (;;) {
        const Block *b = dynamic_cast<const Block *>(at);
        if (b == nullptr) break;
        // [dcl.constexpr]/3: typedefs, static_asserts, using-declarations and null statements may stand beside
        // the return - each leaves an empty block marked declarative.
        const Stmt *only = nullptr;
        for (const StmtPtr &s : b->body()) {
            const Block *nothing = dynamic_cast<const Block *>(s.get());
            if (nothing != nullptr && nothing->declarative()) continue;
            if (only != nullptr) return nullptr;
            only = s.get();
        }
        if (only == nullptr) return nullptr;
        at = only;
    }
    const Return *r = dynamic_cast<const Return *>(at);
    if (r == nullptr || !r->hasValue()) return nullptr;
    return &r->value();
}

bool Parser::constantInitialiser(const Type *t, const Init &in,
                                 long long *out) const {
    // [expr.const]/2: a volatile object is read at run time, const or not.
    if (t == nullptr || !t->isConst() || t->isVolatile() || !t->isInteger()) return false;
    if (in.isList || in.value == nullptr) return false;
    return fold(*in.value, out, in.pos);
}

// The floating twin: a const floating object with a foldable initialiser is
// read back by foldDouble, an integer one being widened on the way in.
bool Parser::constantFloatingInitialiser(const Type *t, const Init &in,
                                         long double *out) const {
    if (t == nullptr || !t->isConst() || t->isVolatile() || !t->isFloating()) return false;
    if (in.isList || in.value == nullptr) return false;
    return foldFloating(*in.value, out);
}

// foldDouble with its two host-precision flags folded away: an answer is an
// answer here, as it is for the integer lane.
bool Parser::foldFloating(const Expr &e, long double *out) const {
    bool past53 = false, x87 = false;
    return foldDouble(e, target_, out, &past53, &x87);
}

long long Parser::narrowTo(long long v, const Type *t) const {
    int bits = t->size(target_) * 8;
    if (bits >= 64) return v;
    unsigned long long mask = (1ULL << bits) - 1;
    unsigned long long kept = static_cast<unsigned long long>(v) & mask;
    if (t->isSigned(target_) && (kept & (1ULL << (bits - 1)))) kept |= ~mask;
    return static_cast<long long>(kept);
}

// The same types, in any order and however often each is written.
bool Parser::DynamicSpec::sameSet(const DynamicSpec &o) const {
    auto within = [](const std::vector<const Type *> &a, const std::vector<const Type *> &b) {
        for (const Type *t : a) {
            bool found = false;
            for (const Type *u : b) found = found || t == u || t->describe() == u->describe();
            if (!found) return false;
        }
        return true;
    };
    return within(types, o.types) && within(o.types, types);
}

// ---------------------------------------------------------------- constant objects
// **[expr.const]/2 lets a literal class's object be worked out while compiling**: a constexpr
// constructor's lowered body runs over its scalar leaves, a pointer being object and offset. A
// bit-field, an address, a union read through another member: none folds, and the need is refused.

// The leaves of `from` in [at, at + size), moved to start at 0 - a member or a base taken whole.
Parser::ConstObject Parser::subObject(const ConstObject &from, int at, int size) {
    ConstObject out;
    for (ConstObject::const_iterator it = from.lower_bound(at);
         it != from.end() && it->first < at + size; ++it)
        out[it->first - at] = it->second;
    return out;
}

void Parser::storeSubObject(ConstObject &into, int at, int size, const ConstObject &what) {
    into.erase(into.lower_bound(at), into.lower_bound(at + size));
    for (const auto &leaf : what) into[at + leaf.first] = leaf.second;
}

std::string Parser::constObjectKey(const Var &v) const {
    return v.isLocal() ? currentFunction_ + "@" + std::to_string(v.offset()) : v.symbol();
}

// `*(c ? &a : &b)`, which an lvalue `?:` is lowered to ([expr.cond]/4): the lvalue the condition picks.
const Expr *Parser::chosenLvalue(const Expr &e, std::size_t pos) const {
    const Unary *u = dynamic_cast<const Unary *>(&e);
    const Conditional *c = u != nullptr && u->op() == '*' ? dynamic_cast<const Conditional *>(&u->operand()) : nullptr;
    long long t = 0;
    if (c == nullptr || !fold(c->cond(), &t, pos)) return nullptr;
    const Unary *arm = dynamic_cast<const Unary *>(t ? &c->thenArm() : &c->elseArm());
    return arm != nullptr && arm->op() == '&' ? &arm->operand() : nullptr;
}

// [dcl.constexpr]/9: `constexpr` makes the object const - `int *const` for `constexpr int *p` - where the
// const the specifiers read it as sits on the pointee, unless `const` was written there too. A pointer to a
// function or through volatile keeps it where it is.
const Type *Parser::constexprObjectType(const Type *t, const Qualifiers &q) {
    if (!q.isConstexpr || mentionsDeduced(t)) return t;
    if (t->isArray()) {
        const Type *e = constexprObjectType(t->pointee(), q);
        return e == t->pointee() ? t : types_.arrayOf(e, t->length());
    }
    if (!t->isPointer()) return t;
    if (q.constWritten) return types_.withConst(t);
    const Type *inner = withoutBaseConst(t->pointee());
    return inner == nullptr ? t : types_.withConst(types_.pointerTo(inner));
}

// The type with the const taken off what its pointers lead to, or null where that is a function.
const Type *Parser::withoutBaseConst(const Type *t) {
    if (t->isFunction() || t->isVolatile()) return nullptr;
    if (!t->isPointer()) return types_.withoutConst(t);
    const Type *in = withoutBaseConst(t->pointee());
    if (in == nullptr) return nullptr;
    const Type *p = types_.pointerTo(in);
    return t->isConst() ? types_.withConst(p) : p;
}

// [expr.const]/3: a constexpr pointer's initialiser is an address constant - into a constant object or a
// string literal, kept under `key` for `*p` to be read; or any other object's or function's, or null.
bool Parser::constantPointerInit(const Init &in, const std::string &key) {
    if (in.isList || in.value == nullptr) return false;
    const bool scratch = constexprFrames_.empty();
    if (scratch) constexprFrames_.push_back(std::vector<ConstexprSlot>(1, ConstexprSlot()));
    if (scratch) constexprFrames_.back()[0].offset = kScratchFrame;
    ConstObjectPtr obj;
    int at = 0;
    const bool into = foldObjectPointer(*in.value, &obj, &at, in.pos);
    if (scratch) constexprFrames_.pop_back();
    if (into) {
        constPointers_[key] = std::make_pair(obj, at);
        return true;
    }
    std::string sym;
    long long v = 0;
    return foldAddress(*in.value, &sym, &v) || fold(*in.value, &v, in.pos);
}

// What a call's argument is worth: a number, an object of class type, or a pointer into one.
bool Parser::foldArgument(const Expr &a, ConstexprSlot *slot, std::size_t pos) const {
    const Type *t = a.type();
    if (t != nullptr && t->unqualified()->isStructOrUnion()) {
        slot->obj = std::make_shared<ConstObject>();
        return foldObject(a, slot->obj.get(), pos);
    }
    // A pointer into a constant object, array or string literal, and the address it holds where it has a name.
    if (t != nullptr && t->isPointer() && !t->pointee()->isFunction()) {
        int at = 0;
        long long off = 0;
        const bool into = foldObjectPointer(a, &slot->obj, &at, pos);
        const bool named = foldAddress(a, &slot->address, &off);
        if (into || named) {
            slot->pointer = true;
            slot->i = into ? at : off;
            if (!into) slot->obj.reset();
            if (into && named && off != at) slot->address.clear();
            return true;
        }
        slot->obj.reset();
        slot->address.clear();
        if (t->pointee()->unqualified()->isStructOrUnion()) return false;
    }
    slot->floating = t != nullptr && t->isFloating();
    return slot->floating ? foldFloating(a, &slot->d) : fold(a, &slot->i, pos);
}

// **Where an lvalue of a constant object lies**: a named one, a slot of the frame, a member of one,
// or what a pointer into one reaches.
bool Parser::objectAddress(const Expr &e, ConstObjectPtr *obj, int *at, std::size_t pos) const {
    if (const Var *v = dynamic_cast<const Var *>(&e)) {
        if (const ConstexprSlot *slot = constexprSlot(*v)) {
            if (slot->obj == nullptr || slot->pointer) return false;
            *obj = slot->obj;
            *at = 0;
            return true;
        }
        // A named local outside any call: inside one, every local name is a parameter or a temporary.
        if (v->isLocal() && !constexprFrames_.empty() && (constexprFrames_.back().empty() ||
                                                          constexprFrames_.back()[0].offset != kScratchFrame))
            return false;
        std::map<std::string, ConstObject>::const_iterator it = constObjects_.find(constObjectKey(*v));
        if (it == constObjects_.end() && !v->isLocal()) it = constObjects_.find(v->name());
        if (it == constObjects_.end()) return false;
        *obj = std::make_shared<ConstObject>(it->second);
        *at = 0;
        return true;
    }
    if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(&e)) {
        if (m->isBitField() || !objectAddress(m->object(), obj, at, pos)) return false;
        *at += m->offset();
        return true;
    }
    if (const Unary *u = dynamic_cast<const Unary *>(&e))
        if (u->op() == '*') return foldObjectPointer(u->operand(), obj, at, pos);
    // [lex.string]/8: a narrow string literal is an array of const char, its terminator included.
    if (const StrLit *s = dynamic_cast<const StrLit *>(&e)) {
        const Type *t = e.type();
        if (t == nullptr || !t->isArray() || t->pointee()->size(target_) != 1) return false;
        ConstObject chars;
        for (long long i = 0; i < t->length(); i++) {
            ConstLeaf leaf;
            leaf.type = t->pointee()->unqualified();
            const unsigned char c = i < static_cast<long long>(s->text().size())
                                  ? static_cast<unsigned char>(s->text()[static_cast<std::size_t>(i)]) : 0;
            leaf.i = leaf.type->isSigned(target_) ? static_cast<signed char>(c) : c;
            chars[static_cast<int>(i)] = leaf;
        }
        *obj = std::make_shared<ConstObject>(std::move(chars));
        *at = 0;
        return true;
    }
    return false;
}

// **A pointer into a constant object**, as the lowering computes one: `&x`, `this`, a cast, the
// arithmetic a base's address is reached by, and the comma that builds a temporary first.
bool Parser::foldObjectPointer(const Expr &e, ConstObjectPtr *obj, int *at, std::size_t pos) const {
    if (const Expr *chosen = chosenLvalue(e, pos)) return foldObjectPointer(*chosen, obj, at, pos);
    if (e.type() != nullptr && e.type()->isArray()) return objectAddress(e, obj, at, pos);
    if (const Var *v = dynamic_cast<const Var *>(&e)) {
        const ConstexprSlot *slot = constexprSlot(*v);
        if (slot == nullptr) {
            // A named constexpr pointer; a local one outside any call, as objectAddress asks.
            if (v->isLocal() && !constexprFrames_.empty() && (constexprFrames_.back().empty() ||
                                                              constexprFrames_.back()[0].offset != kScratchFrame))
                return false;
            std::map<std::string, std::pair<ConstObjectPtr, int> >::const_iterator it =
                constPointers_.find(constObjectKey(*v));
            if (it == constPointers_.end() && !v->isLocal()) it = constPointers_.find(v->name());
            if (it == constPointers_.end()) return false;
            *obj = it->second.first;
            *at = it->second.second;
            return true;
        }
        if (!slot->pointer || slot->obj == nullptr) return false;
        *obj = slot->obj;
        *at = static_cast<int>(slot->i);
        return true;
    }
    if (const Call *c = dynamic_cast<const Call *>(&e)) {
        const ConstexprFn *fn = nullptr;
        if (!enterConstexprCall(*c, &fn, pos)) return false;
        const bool ok = foldObjectPointer(*fn->value, obj, at, pos);
        constexprFrames_.pop_back();
        return ok;
    }
    if (const Unary *u = dynamic_cast<const Unary *>(&e)) {
        if (u->op() != '&') return false;
        if (objectAddress(u->operand(), obj, at, pos)) return true;
        // The address of a value - `make().get()` calls get on the object make returned.
        ConstObject value;
        if (!foldObject(u->operand(), &value, pos)) return false;
        *obj = std::make_shared<ConstObject>(std::move(value));
        *at = 0;
        return true;
    }
    if (const Cast *c = dynamic_cast<const Cast *>(&e)) {
        // An array decayed to its first element.
        if (c->value().type() != nullptr && c->value().type()->isArray()) return objectAddress(c->value(), obj, at, pos);
        return c->value().type() != nullptr && c->value().type()->isPointer() &&
               foldObjectPointer(c->value(), obj, at, pos);
    }
    if (const Binary *b = dynamic_cast<const Binary *>(&e)) {
        long long n = 0;
        const Type *pt = b->lhs().type();
        if ((b->op() != BinOp::Add && b->op() != BinOp::Sub) || pt == nullptr || !pt->isPointer() ||
            !foldObjectPointer(b->lhs(), obj, at, pos) || !fold(b->rhs(), &n, pos))
            return false;
        // The lowering has scaled the index already (pointerAdd): what is added is bytes.
        *at += static_cast<int>(b->op() == BinOp::Add ? n : -n);
        return true;
    }
    // `p ? p + k : 0` - a base's address, null-checked; an object here is never null.
    if (const Conditional *c = dynamic_cast<const Conditional *>(&e)) {
        ConstObjectPtr tested;
        int where = 0;
        long long t = 0;
        if (c->cond().type() != nullptr && c->cond().type()->isPointer()) {
            if (!foldObjectPointer(c->cond(), &tested, &where, pos)) return false;
            t = 1;
        } else if (!fold(c->cond(), &t, pos)) {
            return false;
        }
        return foldObjectPointer(t ? c->thenArm() : c->elseArm(), obj, at, pos);
    }
    if (const Comma *c = dynamic_cast<const Comma *>(&e))
        return runConstexprEffect(c->left(), pos) && foldObjectPointer(c->right(), obj, at, pos);
    return false;
}

// **A class template's constexpr member is compiled where a constant needs it**, not after the
// file as instantiatePending would: that one body is replayed now, its parameters bound.
bool Parser::instantiateForConstant(const std::string &symbol) const {
    Parser &self = const_cast<Parser &>(*this);
    std::string complete = symbol;
    const std::size_t c2 = complete.find("C2E");
    if (c2 != std::string::npos) complete.replace(c2, 3, "C1E");
    for (std::size_t i = 0; i < specializations_.size(); i++) {
        // **A constexpr function template's specialization, made now** -
        // [dcl.constexpr]/6, [temp.inst]/3: `Arr<sq(3)>` needs its body as
        // read; replayed as emission would, and not made again there.
        if (!specializations_[i].isClass) {
            if (specializations_[i].emitted || specializations_[i].explicitly ||
                specializations_[i].symbol != symbol || templates_.count(specializations_[i].templateKey) == 0)
                continue;
            const TemplateDecl decl = templates_.find(specializations_[i].templateKey)->second;
            // Only a constexpr one: any other is made at the end of the file, where the names its
            // body looks up at the point of instantiation are all declared.
            bool isConstexpr = false;
            for (std::size_t t = decl.afterParams; t < tokens_.size() && !tokens_[t].is("(") &&
                                                   tokens_[t].text != decl.name; t++)
                isConstexpr = isConstexpr || tokens_[t].is("constexpr");
            if (!isConstexpr) return false;
            self.specializations_[i].emitted = true;
            const Specialization sp = specializations_[i];
            std::vector<Shadow> undo;
            self.bindTemplateParameters(sp.params, sp.binding, sp.values, sp.packs, &undo);
            std::vector<std::vector<ConstexprSlot> > frames;
            frames.swap(constexprFrames_);
            const std::string wasKey = instantiationKey_, wasOf = instantiationOf_, wasDisplay = instantiationDisplay_;
            const int wasDepth = instantiationDepth_;
            const bool wasInline = replayingInline_;
            const std::vector<std::string> wasStack = namespaceStack_;
            const std::size_t resume = at_;
            // A constant inside an `extern "C"` block asks for a specialization with C++ linkage.
            const int wasC = cLinkage_;
            const bool wasDirect = directLinkage_;
            self.cLinkage_ = 0;
            self.directLinkage_ = false;
            const FunctionState outer = self.captureFunctionState();
            self.clearFunctionState();
            self.instantiationKey_ = sp.key;
            self.instantiationOf_ = sp.name;
            self.instantiationDisplay_ = sp.display;
            self.instantiationDepth_ = sp.depth;
            self.replayingInline_ = true;
            if (!decl.ns.empty()) self.namespaceStack_ = self.namespacePathStack(decl.ns);
            auto restore = [&]() {
                self.restoreFunctionState(outer);
                self.cLinkage_ = wasC;
                self.directLinkage_ = wasDirect;
                self.namespaceStack_ = wasStack;
                self.at_ = resume;
                self.replayingInline_ = wasInline;
                self.instantiationKey_ = wasKey;
                self.instantiationOf_ = wasOf;
                self.instantiationDisplay_ = wasDisplay;
                self.instantiationDepth_ = wasDepth;
                frames.swap(constexprFrames_);
                self.unbindTemplateParameters(undo);
            };
            try {
                self.at_ = sp.start;
                self.topLevel(*current_);
            } catch (...) {
                restore();
                throw;
            }
            restore();
            return true;
        }
        std::vector<PendingBody> &bodies = self.specializations_[i].bodies;
        for (std::size_t b = 0; b < bodies.size(); b++) {
            if (bodies[b].which >= functions_.size() || functions_[bodies[b].which].symbol != complete) continue;
            const PendingBody body = bodies[b];
            bodies.erase(bodies.begin() + static_cast<std::ptrdiff_t>(b));
            self.functions_[body.which].used = true;
            const Specialization sp = specializations_[i];
            std::vector<Shadow> undo;
            self.bindTemplateParameters(sp.params, sp.binding, sp.values, sp.packs, &undo);
            std::vector<std::vector<ConstexprSlot> > frames;
            frames.swap(constexprFrames_);
            const std::string wasKey = instantiationKey_, wasOf = instantiationOf_, wasDisplay = instantiationDisplay_;
            self.instantiationKey_.clear();
            self.instantiationOf_.clear();
            self.instantiationDisplay_.clear();
            const bool wasInline = replayingInline_;
            const bool wasMemberInst = memberTemplateInst_, wasMemberBody = memberTemplateBody_;
            self.memberTemplateInst_ = false;
            self.memberTemplateBody_ = false;
            const std::size_t resume = at_;
            self.replayInlineBodies(std::vector<PendingBody>(1, body));
            self.at_ = resume;
            self.replayingInline_ = wasInline;
            self.memberTemplateInst_ = wasMemberInst;
            self.memberTemplateBody_ = wasMemberBody;
            self.instantiationKey_ = wasKey;
            self.instantiationOf_ = wasOf;
            self.instantiationDisplay_ = wasDisplay;
            frames.swap(constexprFrames_);
            self.unbindTemplateParameters(undo);
            return true;
        }
    }
    return false;
}

// **The value of an expression of class type.**
bool Parser::foldObject(const Expr &e, ConstObject *out, std::size_t pos) const {
    const Type *t = e.type();
    if (t == nullptr || !t->unqualified()->isStructOrUnion()) return false;
    const bool scratch = constexprFrames_.empty();
    if (scratch) constexprFrames_.push_back(std::vector<ConstexprSlot>(1, ConstexprSlot()));
    if (scratch) constexprFrames_.back()[0].offset = kScratchFrame;
    bool ok = false;
    ConstObjectPtr obj;
    int at = 0;
    const auto made = foldedAggregates_.find(&e);
    const Var *list = made != foldedAggregates_.end() ? aggregateTemporaryVar(e) : nullptr;
    if (list != nullptr && list->name() == made->second.first) {
        *out = made->second.second;
        ok = true;
    } else if (const Call *c = dynamic_cast<const Call *>(&e)) {
        const ConstexprFn *fn = nullptr;
        if (enterConstexprCall(*c, &fn, pos)) {
            ok = foldObject(*fn->value, out, pos);
            constexprFrames_.pop_back();
        }
    } else if (const Cast *c = dynamic_cast<const Cast *>(&e)) {
        ok = c->value().type() != nullptr && c->value().type()->unqualified() == t->unqualified() &&
             foldObject(c->value(), out, pos);
    } else if (objectAddress(e, &obj, &at, pos)) {
        *out = subObject(*obj, at, t->size(target_));
        ok = true;
    } else if (valueInitialisedTemporary(e)) {
        // [dcl.init]/8: `P()` of a class no constructor was written for is zero, leaf by leaf.
        ok = constObjectFromInit(t, Init(), 0, out);
    } else if (const MemberAccess *m = dynamic_cast<const MemberAccess *>(&e)) {
        // A member of an object that is itself a value - `make().inner`.
        ConstObject whole;
        ok = !m->isBitField() && foldObject(m->object(), &whole, pos);
        if (ok) *out = subObject(whole, m->offset(), t->size(target_));
    } else if (const Conditional *c = dynamic_cast<const Conditional *>(&e)) {
        // `n == 0 ? P(1) : P(pw(n - 1).r * 2)` - the arm the condition chooses (1.82: a class value through a
        // recursive constexpr function did not fold).
        long long chosen = 0;
        ok = fold(c->cond(), &chosen, pos) && foldObject(chosen ? c->thenArm() : c->elseArm(), out, pos);
    }
    if (scratch) constexprFrames_.pop_back();
    return ok;
}

// The two shapes classTemporary gives `P()`: the `$vinit` temporary, or `*(zeroing, &$vinit)`.
bool Parser::valueInitialisedTemporary(const Expr &e) {
    const Var *v = dynamic_cast<const Var *>(&e);
    if (v == nullptr) {
        const Unary *u = dynamic_cast<const Unary *>(&e);
        const Comma *c = u != nullptr && u->op() == '*' ? dynamic_cast<const Comma *>(&u->operand()) : nullptr;
        const Unary *at = c != nullptr ? dynamic_cast<const Unary *>(&c->right()) : nullptr;
        v = at != nullptr && at->op() == '&' ? dynamic_cast<const Var *>(&at->operand()) : nullptr;
    }
    return v != nullptr && v->isLocal() && v->name() == "$vinit";
}

// **One scalar of a constant object**, the type the read asks for and the leaf agreeing on it.
bool Parser::foldLeaf(const Expr &e, ConstLeaf *out, std::size_t pos) const {
    const Type *t = e.type();
    if (t == nullptr || !t->isArithmetic()) return false;
    const MemberAccess *m = dynamic_cast<const MemberAccess *>(&e);
    if (m != nullptr && m->isBitField()) return false;
    const bool scratch = constexprFrames_.empty();
    if (scratch) constexprFrames_.push_back(std::vector<ConstexprSlot>(1, ConstexprSlot()));
    if (scratch) constexprFrames_.back()[0].offset = kScratchFrame;
    ConstObjectPtr obj;
    int at = 0;
    bool ok = objectAddress(e, &obj, &at, pos);
    if (!ok && m != nullptr) {
        ConstObject whole;
        ok = foldObject(m->object(), &whole, pos);
        if (ok) { obj = std::make_shared<ConstObject>(std::move(whole)); at = m->offset(); }
    }
    if (scratch) constexprFrames_.pop_back();
    if (!ok) return false;
    ConstObject::const_iterator it = obj->find(at);
    if (it == obj->end() || it->second.type->isFloating() != t->isFloating() ||
        it->second.type->size(target_) != t->size(target_))
        return false;
    *out = it->second;
    return true;
}

// **A constexpr constructor run on the object its first argument points at.**
bool Parser::constructConst(const Call &c, std::size_t pos) const {
    std::map<std::string, ConstexprCtor>::const_iterator it = constexprCtors_.find(c.symbol());
    if (it == constexprCtors_.end() && instantiateForConstant(c.symbol())) it = constexprCtors_.find(c.symbol());
    if (it == constexprCtors_.end() || c.args().empty()) return false;
    const ConstexprCtor &ctor = it->second;
    if (ctor.slots.size() + 1 != c.args().size()) return false;
    if (constexprFrames_.size() >= kMaxConstexprCalls)
        src_.fail(pos, "this constant expression is more than " + std::to_string(kMaxConstexprCalls) + " calls deep "
                       "- a 'constexpr' function that never stops recursing cannot be worked out while compiling");
    // A temporary the comma around this call reads back is made here, in the caller's frame.
    ConstexprSlot self;
    self.offset = ctor.thisSlot;
    self.pointer = true;
    int at = 0;
    if (!foldObjectPointer(*c.args()[0], &self.obj, &at, pos)) {
        const Unary *u = dynamic_cast<const Unary *>(c.args()[0].get());
        const Var *v = u != nullptr && u->op() == '&' ? dynamic_cast<const Var *>(&u->operand()) : nullptr;
        if (v == nullptr || !v->isLocal() || constexprFrames_.empty()) return false;
        ConstexprSlot made;
        made.offset = v->offset();
        made.obj = std::make_shared<ConstObject>();
        constexprFrames_.back().push_back(made);
        self.obj = made.obj;
        at = 0;
    }
    self.i = at;
    std::vector<ConstexprSlot> frame(1, self);
    for (std::size_t i = 1; i < c.args().size(); i++) {
        ConstexprSlot slot;
        slot.offset = ctor.slots[i - 1];
        if (!foldArgument(*c.args()[i], &slot, pos)) return false;
        frame.push_back(slot);
    }
    constexprFrames_.push_back(frame);
    const bool ok = runConstexpr(*ctor.body, pos);
    constexprFrames_.pop_back();
    return ok;
}

// The statements a constructor's body and a declaration's initialisation are lowered to.
bool Parser::runConstexpr(const Stmt &s, std::size_t pos) const {
    if (const Block *b = dynamic_cast<const Block *>(&s)) {
        for (const StmtPtr &x : b->body())
            if (!runConstexpr(*x, pos)) return false;
        return true;
    }
    if (const ExprStmt *x = dynamic_cast<const ExprStmt *>(&s)) return runConstexprEffect(x->expr(), pos);
    return false;
}

bool Parser::runConstexprEffect(const Expr &e, std::size_t pos) const {
    if (const Comma *c = dynamic_cast<const Comma *>(&e))
        return runConstexprEffect(c->left(), pos) && runConstexprEffect(c->right(), pos);
    if (const Call *c = dynamic_cast<const Call *>(&e)) {
        // A function returning an object into a slot of the frame - the slot holds its value from here.
        if (c->resultSlot() != 0 && c->type() != nullptr && c->type()->unqualified()->isStructOrUnion() &&
            !constexprFrames_.empty()) {
            ConstexprSlot made;
            made.offset = c->resultSlot();
            made.obj = std::make_shared<ConstObject>();
            if (!foldObject(*c, made.obj.get(), pos)) return false;
            constexprFrames_.back().push_back(made);
            return true;
        }
        return constructConst(*c, pos);
    }
    const Assign *a = dynamic_cast<const Assign *>(&e);
    if (a == nullptr || a->type() == nullptr) return false;
    const Type *t = a->target().type()->unqualified();
    // A pointer the lowering keeps in a temporary of its own - a base's address on its way.
    const Var *temp = dynamic_cast<const Var *>(&a->target());
    if (t->isPointer() && (temp == nullptr || !temp->isLocal())) {
        // A pointer member: the address it is given, or null.
        ConstObjectPtr obj;
        int at = 0;
        ConstLeaf leaf;
        leaf.type = t;
        if (!objectAddress(a->target(), &obj, &at, pos) ||
            (!foldAddress(a->value(), &leaf.address, &leaf.i) && !(fold(a->value(), &leaf.i, pos) && leaf.i == 0)))
            return false;
        storeSubObject(*obj, at, t->size(target_), ConstObject());
        (*obj)[at] = leaf;
        return true;
    }
    if (t->isPointer()) {
        const Var *v = temp;
        if (v == nullptr || !v->isLocal() || constexprFrames_.empty()) return false;
        ConstexprSlot slot;
        slot.offset = v->offset();
        int at = 0;
        if (!foldObjectPointer(a->value(), &slot.obj, &at, pos)) return false;
        slot.pointer = true;
        slot.i = at;
        std::vector<ConstexprSlot> &frame = constexprFrames_.back();
        for (std::size_t i = 0; i < frame.size(); i++)
            if (frame[i].offset == slot.offset && frame[i].global.empty()) { frame[i] = slot; return true; }
        frame.push_back(slot);
        return true;
    }
    ConstObjectPtr obj;
    int at = 0;
    if (!objectAddress(a->target(), &obj, &at, pos)) return false;
    if (t->isStructOrUnion()) {
        ConstObject value;
        if (!foldObject(a->value(), &value, pos)) return false;
        storeSubObject(*obj, at, t->size(target_), value);
        return true;
    }
    if (!t->isArithmetic()) return false;
    ConstLeaf leaf;
    leaf.type = t;
    if (t->isFloating()) {
        if (!foldFloating(a->value(), &leaf.d)) return false;
    } else {
        if (!fold(a->value(), &leaf.i, pos)) return false;
        leaf.i = t->isBool() ? leaf.i != 0 : narrowTo(leaf.i, t);
    }
    storeSubObject(*obj, at, t->size(target_), ConstObject());
    (*obj)[at] = leaf;
    return true;
}

// **The object a declaration builds**, from the statements it was lowered to.
bool Parser::foldDeclaredObject(const std::vector<const Stmt *> &init, const Var &target, ConstObject *out,
                                std::size_t pos) {
    ConstexprSlot self;
    self.offset = target.isLocal() ? target.offset() : 0;
    if (!target.isLocal()) self.global = target.symbol();
    self.obj = std::make_shared<ConstObject>();
    ConstexprSlot scratch;
    scratch.offset = kScratchFrame;
    std::vector<ConstexprSlot> frame;
    frame.push_back(scratch);
    frame.push_back(self);
    constexprFrames_.push_back(frame);
    bool ok = true;
    for (const Stmt *s : init) ok = ok && runConstexpr(*s, pos);
    constexprFrames_.pop_back();
    if (ok) *out = *self.obj;
    return ok;
}

// An aggregate's braced list, leaf by leaf, as flattenInit lays it down.
bool Parser::constObjectFromInit(const Type *t, const Init &in, int base, ConstObject *out) const {
    t = t->unqualified();
    if (in.isList) {
        if (t->isArray()) {
            const int w = t->pointee()->size(target_);
            for (std::size_t i = 0; i < in.items.size(); i++)
                if (!constObjectFromInit(t->pointee(), in.items[i], base + static_cast<int>(i) * w, out))
                    return false;
            for (long long i = static_cast<long long>(in.items.size()); i < t->length(); i++)
                if (!constObjectFromInit(t->pointee(), Init(), base + static_cast<int>(i) * w, out)) return false;
            return true;
        }
        if (!t->isStructOrUnion() || t->kind() == Kind::Union) return false;
        static const Init none;
        std::size_t k = 0;
        for (const Member &m : t->members()) {
            if (m.declaredIn != nullptr || m.isBitField()) return false;
            if (!constObjectFromInit(m.type, k < in.items.size() ? in.items[k] : none, base + m.offset, out))
                return false;
            k++;
        }
        return k >= in.items.size();
    }
    if (t->isArray() || t->isStructOrUnion()) {
        if (in.value != nullptr) {
            ConstObject whole;
            if (!foldObject(*in.value, &whole, in.pos)) return false;
            storeSubObject(*out, base, t->size(target_), whole);
            return true;
        }
        // Left out of the list: zero, leaf by leaf.
        if (t->isArray()) {
            for (long long i = 0; i < t->length(); i++)
                if (!constObjectFromInit(t->pointee(), Init(), base + static_cast<int>(i * t->pointee()->size(target_)), out))
                    return false;
            return true;
        }
        for (const Member &m : t->members())
            if (m.declaredIn == nullptr && !constObjectFromInit(m.type, Init(), base + m.offset, out)) return false;
        return true;
    }
    if (t->isPointer()) {
        ConstLeaf leaf;
        leaf.type = t;
        if (in.value != nullptr && !foldAddress(*in.value, &leaf.address, &leaf.i) &&
            !(fold(*in.value, &leaf.i, in.pos) && leaf.i == 0))
            return false;
        (*out)[base] = leaf;
        return true;
    }
    if (!t->isArithmetic()) return false;
    ConstLeaf leaf;
    leaf.type = t;
    if (in.value != nullptr && (t->isFloating() ? !foldFloating(*in.value, &leaf.d) : !fold(*in.value, &leaf.i, in.pos)))
        return false;
    if (!t->isFloating()) leaf.i = t->isBool() ? leaf.i != 0 : narrowTo(leaf.i, t);
    (*out)[base] = leaf;
    return true;
}

// The bytes a constant object is laid down as, each leaf the way flattenInit writes a scalar.
void Parser::constObjectPieces(const ConstObject &o, std::vector<GlobalPiece> &out) {
    for (const auto &leaf : o) {
        if (leaf.second.type->isPointer()) {
            GlobalPiece p;
            p.offset = leaf.first;
            p.size = leaf.second.type->size(target_);
            p.value = leaf.second.i;
            p.symbol = leaf.second.address;
            out.push_back(p);
            continue;
        }
        Init in;
        Num *n = leaf.second.type->isFloating() ? new Num(leaf.second.d) : new Num(leaf.second.i);
        n->setType(leaf.second.type);
        in.value.reset(n);
        flattenScalar(leaf.second.type, in, leaf.first, out);
    }
}

// **The one answer when it does not fold**: [dcl.constexpr]/9 wants a constant, and what this
// compiler can fold of a class is a constexpr constructor's member initialisers and an aggregate's list.
void Parser::refuseConstexprClassObject(const std::string &name, std::size_t pos) const {
    src_.fail(pos, "'" + name + "' is 'constexpr', and its value could not be worked out while compiling - "
                   "either its initialiser is not a constant expression, or it needs what folding an object "
                   "of class type does not support yet: a pointer, reference, union or bit-field member, an "
                   "array of class objects, or a constructor that is not 'constexpr'");
}
