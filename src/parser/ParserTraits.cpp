// The type-trait built-ins: the questions [meta.unary.prop] and [meta.rel] ask that C++11 cannot
// answer in itself, under the names clang and cl both give them. Phase 7a's <type_traits> is
// written over these, as libc++'s and Microsoft's are.
#include "Parser.h"
#include "ParserInternal.h"
#include "../Source.h"

#include <cstring>

namespace {

enum TraitKind {
    IsClass, IsUnion, IsEnum, IsPod, IsEmpty, IsPolymorphic, IsAbstract, IsFinal, IsStandardLayout,
    IsTrivial, IsTriviallyCopyable, IsLiteralType, HasVirtualDestructor, IsDestructible,
    IsNothrowDestructible, IsTriviallyDestructible, IsBaseOf, IsConvertible, IsAssignable,
    IsNothrowAssignable, IsTriviallyAssignable, IsConstructible, IsNothrowConstructible,
    IsTriviallyConstructible
};

// `operands` is how many type-ids a trait takes: 1, 2, or -1 for one and any number after it.
struct TraitSpelling { const char *name; TraitKind trait; int operands; };

const TraitSpelling kTraits[] = {
    { "__is_class", IsClass, 1 },
    { "__is_union", IsUnion, 1 },
    { "__is_enum", IsEnum, 1 },
    { "__is_pod", IsPod, 1 },
    { "__is_empty", IsEmpty, 1 },
    { "__is_polymorphic", IsPolymorphic, 1 },
    { "__is_abstract", IsAbstract, 1 },
    { "__is_final", IsFinal, 1 },
    { "__is_standard_layout", IsStandardLayout, 1 },
    { "__is_trivial", IsTrivial, 1 },
    { "__is_trivially_copyable", IsTriviallyCopyable, 1 },
    { "__is_literal", IsLiteralType, 1 },
    { "__is_literal_type", IsLiteralType, 1 },
    { "__has_virtual_destructor", HasVirtualDestructor, 1 },
    { "__is_destructible", IsDestructible, 1 },
    { "__is_nothrow_destructible", IsNothrowDestructible, 1 },
    { "__is_trivially_destructible", IsTriviallyDestructible, 1 },
    { "__is_base_of", IsBaseOf, 2 },
    { "__is_convertible", IsConvertible, 2 },
    { "__is_convertible_to", IsConvertible, 2 },
    { "__is_assignable", IsAssignable, 2 },
    { "__is_nothrow_assignable", IsNothrowAssignable, 2 },
    { "__is_trivially_assignable", IsTriviallyAssignable, 2 },
    { "__is_constructible", IsConstructible, -1 },
    { "__is_nothrow_constructible", IsNothrowConstructible, -1 },
    { "__is_trivially_constructible", IsTriviallyConstructible, -1 },
};

const TraitSpelling *findTrait(const std::string &word) {
    for (const TraitSpelling &t : kTraits)
        if (word == t.name) return &t;
    return nullptr;
}

// What the class questions are asked of: the element under every array level, without its cv.
const Type *elementOf(const Type *t) {
    return t->innermostElement()->unqualified();
}

// A class or a union - a pointer to a member function wears a struct's shape and is neither.
bool classType(const Type *t) {
    return t->isStructOrUnion() && !t->isMemberFunctionPointer();
}

bool scalarType(const Type *t) {
    return t->isScalar() || t->isMemberPointer() || t->isMemberFunctionPointer();
}

// A data member the class wrote itself, not one it has from a base.
bool ownMember(const Member &m) {
    return m.declaredIn == nullptr && m.inVirtualBase == nullptr;
}

bool derivesFrom(const Type *d, const Type *b) {
    for (const Type::BaseSpec &s : d->unqualified()->bases())
        if (s.type->unqualified() == b || derivesFrom(s.type, b)) return true;
    return false;
}

}

bool Parser::isTypeTraitName(const std::string &word) {
    return findTrait(word) != nullptr;
}

std::vector<const Type *> Parser::traitOperands() {
    expect("(");
    std::vector<const Type *> ops;
    if (consume(")")) return ops;
    auto typeId = [&]() {
        StorageClass sc;
        const Type *t = declarator(specifiers(&sc), true).type;
        // The declarator answers `T()` as T, its caller asked to tell: here it is the function type.
        if (!t->isFunction() && endsInEmptyCall())
            t = types_.functionType(t, std::vector<const Type *>(), false);
        return t;
    };
    for (;;) {
        // `Args...`: one type per member of the pack.
        std::size_t members = 0;
        const std::size_t end = packPatternEnd(false, &members);
        if (end != 0) {
            const std::size_t from = at_;
            for (std::size_t m = 0; m < members; m++) {
                std::vector<Shadow> undo;
                struct Unbind {
                    Parser *p; std::vector<Shadow> *u;
                    ~Unbind() { p->unbindTemplateParameters(*u); }
                } unbind{ this, &undo };
                bindPatternMember(from, end, m, &undo);
                ops.push_back(typeId());
                if (peek().kind != TokenKind::End)
                    src_.fail(peek().pos, "this pack expansion's pattern is not one type");
            }
            at_ = end;
        } else {
            ops.push_back(typeId());
        }
        if (consume(")")) return ops;
        expect(",");
    }
}

// [meta.unary.prop]/3, [meta.rel]/2: each operand complete, cv void, or an array of unknown bound.
void Parser::traitNeedsComplete(const Type *t, const std::string &name, std::size_t pos) {
    if (t->isReference()) return;
    realizeMemberClass(t, true);
    const Type *e = elementOf(t);
    if (e->isVoid() || e->isFunction() || !classType(e) || e->isComplete()) return;
    src_.fail(pos, "'" + name + "' needs a complete type, and '" + e->describe() + "' is incomplete here "
                   "- [meta.unary.prop]/3");
}

ExprPtr Parser::typeTrait() {
    const std::size_t pos = peek().pos;
    const std::string name = peek().text;
    const TraitSpelling *spelled = findTrait(name);
    at_++;
    const std::vector<const Type *> ops = traitOperands();
    const bool counted = spelled->operands < 0 ? !ops.empty()
                                               : ops.size() == static_cast<std::size_t>(spelled->operands);
    if (!counted)
        src_.fail(pos, "'" + name + "' takes " +
                       (spelled->operands == 1 ? std::string("one type")
                        : spelled->operands == 2 ? std::string("two types")
                        : std::string("a type and the types of its arguments")) +
                       ", and is given " + std::to_string(ops.size()));
    if (patternOnly_)
        for (const Type *t : ops)
            if (dependentType(t))
                src_.fail(pos, "'" + name + "' of a template parameter is not supported yet in a template's "
                               "signature - in its body, or in a class template, it is");
    const TraitKind trait = spelled->trait;
    if (trait == IsBaseOf) {
        const Type *b = ops[0]->unqualified(), *d = ops[1]->unqualified();
        if (classType(b) && classType(d) && b != d && b->kind() != Kind::Union && d->kind() != Kind::Union)
            traitNeedsComplete(d, name, pos);
    } else if (trait != IsClass && trait != IsUnion && trait != IsEnum) {
        for (const Type *t : ops) traitNeedsComplete(t, name, pos);
    }
    ExprPtr n(new Num(typeTraitHolds(trait, ops, pos) ? 1LL : 0LL));
    n->setType(types_.get(Kind::Bool));
    return n;
}

// [meta.trans.other]: `__underlying_type(E)`, the integer type an enumeration is held in.
const Type *Parser::underlyingTypeSpecifier() {
    const std::size_t pos = peek().pos;
    at_++;
    expect("(");
    StorageClass sc;
    const Type *e = declarator(specifiers(&sc), true).type;
    expect(")");
    if (patternOnly_ && dependentType(e))
        src_.fail(pos, "'__underlying_type' of a template parameter is not supported yet in a template's "
                       "signature - in its body, or in a class template, it is");
    if (!e->unqualified()->isEnumeration())
        src_.fail(pos, "'__underlying_type' takes an enumeration, and '" + e->describe() + "' is not one");
    return types_.get(e->unqualified()->kind());
}

ExprPtr Parser::traitOperand(const Type *t) {
    const Type *object = t->isReference() ? t->referent() : t;
    // A function is named as a function's name is, by its address - an lvalue even through `F &&`, [expr]/6.
    if (object->isFunction()) {
        ExprPtr f(Var::global("$trait"));
        f->setType(object);
        ExprPtr at(new Unary('&', std::move(f)));
        at->setType(types_.pointerTo(object));
        return at;
    }
    const bool lvalue = t->isReference() && !t->isRValueReference();
    ExprPtr none(new Num(0LL));
    none->setType(types_.pointerTo(object));
    ExprPtr e(new Unary('*', std::move(none)));
    e->setType(object);
    if (!lvalue) e->setXvalue();
    return e;
}

// **Whether the expression a trait asks about is well-formed**, read in a
// thrown-away trial from tokens naming its operands, outside every program
// class ([meta.unary.prop]/6, [meta.rel]/4); `nothrow` as `noexcept(e)`.
bool Parser::traitWellFormed(TraitForm form, const Type *to, const std::vector<const Type *> &operands,
                             std::size_t pos, bool *nothrow) {
    std::vector<Token> spelled;
    auto put = [&](TokenKind k, const std::string &text) {
        Token t;
        t.kind = k;
        t.text = text;
        t.pos = pos;
        spelled.push_back(t);
    };
    if (form == kTraitCast) put(TokenKind::Punct, "(");
    for (std::size_t i = 0; i < operands.size(); i++) {
        if (i > 0) put(TokenKind::Punct, form == kTraitAssign ? "=" : ",");
        put(TokenKind::Ident, "$trait" + std::to_string(i));
    }
    if (form == kTraitConstruct || form == kTraitCast) put(TokenKind::Punct, ")");
    put(TokenKind::End, std::string());

    struct Unrelated {
        Parser *p;
        std::vector<const Type *> operands, classes;
        const Type *cls;
        std::string function, instantiation;
        std::vector<char> used;
        int mayThrow;
        FrameSave frame;
        // The trial's temporaries and their guards live in the trial's frame: left on the function's lists,
        // the function zeroed those guards on entry in its own frame (`is_convertible` asked inside `make`
        // wrote over its parameters).
        std::vector<Temporary> temps, statementTemps;
        std::vector<int> guards;
        Unrelated(Parser *pp, const std::vector<const Type *> &ops)
            : p(pp), operands(pp->traitOperands_), classes(pp->classStack_), cls(pp->currentClass_),
              function(pp->currentFunction_), instantiation(pp->instantiationOf_), mayThrow(pp->mayThrow_),
              frame(pp->openFrame()), temps(pp->pendingTemps_), statementTemps(pp->statementTemps_),
              guards(pp->guardSlots_) {
            p->holdUses(used);
            p->traitOperands_ = ops;
            p->classStack_.clear();
            p->currentClass_ = nullptr;
            p->currentFunction_.clear();
            p->instantiationOf_.clear();
            p->mayThrow_ = 0;
        }
        ~Unrelated() {
            p->pendingTemps_ = temps;
            p->statementTemps_ = statementTemps;
            p->guardSlots_ = guards;
            p->closeFrame(frame);
            p->mayThrow_ = mayThrow;
            p->instantiationOf_ = instantiation;
            p->currentFunction_ = function;
            p->currentClass_ = cls;
            p->classStack_ = classes;
            p->traitOperands_ = operands;
            p->releaseUses(used);
        }
    };

    Unrelated scope(this, operands);
    bool ok = true;
    try {
        Trial trial(this);
        Discarded held(this);
        Unevaluated unevaluated(this);
        const std::size_t start = tokens_.size();
        tokens_.insert(tokens_.end(), spelled.begin(), spelled.end());
        at_ = start;
        ExprPtr e;
        if (form == kTraitConstruct) {
            e = classTemporary(to, pos);
        } else if (form == kTraitCast) {
            e = functionalCast(to, pos);
        } else if (form == kTraitAssign) {
            e = assign();
        } else {
            std::vector<Temporary> destroy;
            e = copyInitialise(to, assign(), pos, "the conversion", destroy);
        }
        if (peek().kind != TokenKind::End) ok = false;
    } catch (const SubstitutionFailure &f) {
        if (f.unsupported) src_.fail(f.pos, f.why);
        ok = false;
    }
    *nothrow = ok && mayThrow_ == 0;
    return ok;
}

void Parser::holdUses(std::vector<char> &used) {
    usesHeld_++;
    used.resize(functions_.size());
    for (std::size_t i = 0; i < used.size(); i++) used[i] = functions_[i].used ? 1 : 0;
}

void Parser::releaseUses(const std::vector<char> &used) {
    usesHeld_--;
    // What a member template's body, read in between, called is kept beside that specialization where the body
    // is read (instantiateMemberTemplate) and asked for again if the program turns out to use it.
    // What the trial would have called is not used by the program for being asked about.
    for (std::size_t i = 0; i < used.size() && i < functions_.size(); i++) functions_[i].used = used[i] != 0;
    // Nor anything the trial made (its marks were just put back)
    for (std::size_t i = used.size(); i < functions_.size(); i++) functions_[i].used = false;
}

// [class.dtor]/11 as [meta.unary.prop] asks it: a destructor that is there, not deleted, and public.
bool Parser::traitDestructible(const Type *t) const {
    if (t->isReference()) return true;
    const Type *u = t->unqualified();
    if (u->isVoid() || u->isFunction() || (u->isArray() && u->length() < 0)) return false;
    const Type *c = elementOf(u);
    if (!classType(c)) return true;
    const Signature *d = destructorOf(c);
    return d == nullptr || (!d->deleted && d->access == Access::Public);
}

// [meta.unary.prop]: `T t(create<Args>()...);` - built, then destroyed, with nothing of T's out of reach.
bool Parser::traitConstructible(const Type *t, const std::vector<const Type *> &args, std::size_t pos,
                                bool *nothrow, bool *trivial) {
    *nothrow = false;
    *trivial = false;
    for (const Type *a : args)
        if (a->unqualified()->isVoid()) return false;
    const Type *u = t->unqualified();
    if (u->isVoid() || u->isFunction()) return false;
    if (t->isReference()) {
        if (args.size() != 1) return false;
        bool quiet = false;
        if (!traitWellFormed(kTraitConvert, t, args, pos, &quiet)) return false;
        *nothrow = quiet;
        *trivial = true;
        return true;
    }
    if (u->isArray()) {
        if (u->length() < 0 || !args.empty()) return false;
        return traitConstructible(u->innermostElement(), args, pos, nothrow, trivial);
    }
    if (!classType(u)) {
        bool quiet = true, ok = args.empty();
        // A conversion function the class source has is taken explicit or not - [over.match.conv]/1.
        if (args.size() == 1)
            ok = classType(args[0]->isReference() ? args[0]->referent()->unqualified() : args[0]->unqualified())
                     ? traitWellFormed(kTraitCast, u, args, pos, &quiet)
                     : traitWellFormed(kTraitConvert, u, args, pos, &quiet);
        *nothrow = ok && quiet;
        // a class's conversion function is a function called: not trivial ([meta.unary.prop]; 1.75)
        *trivial = ok && !(args.size() == 1 &&
                           classType(args[0]->isReference() ? args[0]->referent()->unqualified() : args[0]->unqualified()));
        return ok;
    }
    if (u->abstract() || !traitDestructible(u) || (args.empty() && implicitDefaultDeleted(u))) return false;
    bool quiet = false;
    if (!traitWellFormed(kTraitConstruct, u, args, pos, &quiet)) return false;
    *nothrow = quiet;
    if (args.empty()) {
        *trivial = trivialSpecial(u, kDefaultCtor);
    } else if (args.size() == 1) {
        const Type *a = args[0];
        const Type *from = a->isReference() ? a->referent() : a;
        if (from->unqualified() == u || derivesFrom(from->unqualified(), u)) {
            const bool rvalue = !(a->isReference() && !a->isRValueReference());
            *trivial = trivialSpecial(u, rvalue && !from->isConst() ? kMoveCtor : kCopyCtor);
        }
    }
    return true;
}

// [class.copy]/2-3 and /17-/19: the constructors and assignments of a class that copy or move it, a
// default constructor ([class.ctor]/5) and the destructor. Templates are never any of these.
std::vector<const Parser::Signature *> Parser::specialMembers(const Type *cls, SpecialMember which) const {
    std::vector<const Signature *> out;
    const Type *c = cls->unqualified();
    if (which == kDestructor) {
        if (const Signature *d = destructorOf(c)) out.push_back(d);
        return out;
    }
    const bool ctor = which == kDefaultCtor || which == kCopyCtor || which == kMoveCtor;
    const std::vector<std::size_t> *set =
        overloadsOf(ctor ? constructorKey(c->tag()) : assignmentKey(c->tag()));
    if (set == nullptr) return out;
    for (std::size_t i : *set) {
        const Signature &f = functions_[i];
        if (which == kDefaultCtor) {
            if (leastArguments(f) == 0) out.push_back(&f);
            continue;
        }
        if (f.fromTemplate || f.params.empty() || leastArguments(f) > 1) continue;
        if (!ctor && f.params.size() != 1) continue;
        const Type *p = f.params[0];
        const bool moves = which == kMoveCtor || which == kMoveAssign;
        if (p->isReference()) {
            if (p->referent()->unqualified() != c || p->isRValueReference() != moves) continue;
        } else if (ctor || moves || p->unqualified() != c) {
            continue;
        }
        out.push_back(&f);
    }
    return out;
}

// [class.copy]/9 and /20: a move is declared implicitly only where no copy, assignment or destructor was.
bool Parser::implicitMove(const Type *cls) const {
    const SpecialMember suppress[] = { kCopyCtor, kCopyAssign, kMoveCtor, kMoveAssign, kDestructor };
    for (SpecialMember s : suppress)
        for (const Signature *f : specialMembers(cls, s))
            if (!f->implicit) return false;
    return true;
}

// **[class.ctor]/5, [class.copy]/12 and /25, [class.dtor]/5**: not user-provided, no virtual function or
// base (for the destructor, not virtual), no member initialiser (for the default constructor), and every
// direct base and member's own trivial. A move a class does not have is its copy.
bool Parser::trivialSpecial(const Type *t, SpecialMember which) const {
    const Type *c = elementOf(t);
    if (!classType(c)) return true;
    const std::vector<const Signature *> declared = specialMembers(c, which);
    for (const Signature *f : declared)
        if (!f->implicit && !f->deleted) return false;
    if ((which == kMoveCtor || which == kMoveAssign) && declared.empty() && !implicitMove(c))
        return trivialSpecial(c, which == kMoveCtor ? kCopyCtor : kCopyAssign);
    if (which == kDestructor ? virtualDestructor(c) : c->hasVptr()) return false;
    for (const Member &m : c->members()) {
        if (!ownMember(m)) continue;
        if (which == kDefaultCtor) {
            // [class.ctor]/5: a reference or a const scalar with no initialiser deletes it, one with is work.
            if (memberInit_.count(c->tag() + "::" + m.name) != 0 || m.type->isReference()) return false;
            if (m.type->innermostElement()->isConst() && !classType(elementOf(m.type))) return false;
            if (!hasTrivialDefaultConstructor(m.type)) return false;
        } else if (!m.type->isReference() && !trivialSpecial(m.type, which)) {
            return false;
        }
    }
    for (const Type::BaseSpec *b : c->directBases())
        if (which == kDefaultCtor ? !hasTrivialDefaultConstructor(b->type) : !trivialSpecial(b->type, which))
            return false;
    return true;
}

// A default constructor that is there - none, where the class declared another - and trivial.
bool Parser::hasTrivialDefaultConstructor(const Type *cls) const {
    const Type *c = elementOf(cls);
    if (!classType(c)) return true;
    const std::vector<const Signature *> defaults = specialMembers(c, kDefaultCtor);
    if (defaults.empty())
        if (const std::vector<std::size_t> *set = overloadsOf(constructorKey(c->tag())))
            for (std::size_t i : *set)
                if (!functions_[i].implicit) return false;
    for (const Signature *f : defaults)
        if (f->deleted) return false;
    return trivialSpecial(c, kDefaultCtor);
}

bool Parser::virtualDestructor(const Type *cls) const {
    const Type *c = cls->unqualified();
    const Signature *d = destructorOf(c);
    if (d != nullptr && d->isVirtual) return true;
    for (const Type::BaseSpec &b : c->bases())
        if (virtualDestructor(b.type)) return true;
    return false;
}

// [class]/6 and [basic.types]/9.
bool Parser::triviallyCopyable(const Type *t) const {
    if (t->isReference()) return false;
    const Type *c = elementOf(t);
    if (scalarType(c)) return true;
    if (!classType(c)) return false;
    const Signature *d = destructorOf(c);
    if (d != nullptr && d->deleted) return false;
    return trivialSpecial(c, kCopyCtor) && trivialSpecial(c, kMoveCtor) && trivialSpecial(c, kCopyAssign) &&
           trivialSpecial(c, kMoveAssign) && trivialSpecial(c, kDestructor);
}

// [class]/7.
bool Parser::standardLayout(const Type *t) const {
    if (t->isReference()) return false;
    const Type *c = elementOf(t);
    if (scalarType(c)) return true;
    if (!classType(c) || c->hasVptr()) return false;
    for (const Type::BaseSpec *b : c->directBases())
        if (!standardLayout(b->type)) return false;
    // Every data member written by one class of the hierarchy, all with one access.
    const Type *owner = nullptr;
    bool anyOwner = false;
    const Member *first = nullptr;
    for (const Member &m : c->members()) {
        const Type *by = m.declaredIn != nullptr ? m.declaredIn->unqualified() : c;
        if (anyOwner && by != owner) return false;
        owner = by;
        anyOwner = true;
        if (first == nullptr) first = &m;
        else if (m.access != first->access) return false;
        if (m.type->isReference() || !standardLayout(m.type)) return false;
    }
    // No base of the type the first data member has - [class]/7, last bullet.
    if (first != nullptr) {
        const Type *f = first->type->unqualified();
        for (const Type::BaseSpec &b : c->bases())
            if (b.type->unqualified() == f) return false;
    }
    return true;
}

// [class.ctor]/5: no default constructor, or the one the class did not write deleted - a reference or a const
// scalar member with no initialiser, or a member or base with none of its own.
bool Parser::implicitDefaultDeleted(const Type *cls) const {
    const Type *c = elementOf(cls);
    if (!classType(c)) return false;
    const std::vector<const Signature *> defaults = specialMembers(c, kDefaultCtor);
    for (const Signature *f : defaults)
        if (!f->implicit) return f->deleted;
    if (defaults.empty())
        if (const std::vector<std::size_t> *set = overloadsOf(constructorKey(c->tag())))
            for (std::size_t i : *set)
                if (!functions_[i].implicit) return true;
    for (const Member &m : c->members()) {
        if (!ownMember(m) || memberInit_.count(c->tag() + "::" + m.name) != 0) continue;
        if (m.type->isReference()) return true;
        if (m.type->innermostElement()->isConst() && !classType(elementOf(m.type))) return true;
        if (implicitDefaultDeleted(m.type)) return true;
    }
    for (const Type::BaseSpec *b : c->directBases())
        if (implicitDefaultDeleted(b->type)) return true;
    return false;
}

// [dcl.constexpr]/4 for the default constructor a class did not write, as clang reads C++11: no virtual base,
// and every base and member built by a constexpr one or given an initialiser.
bool Parser::implicitDefaultIsConstexpr(const Type *cls) const {
    const Type *c = elementOf(cls);
    if (!classType(c)) return false;
    const std::vector<const Signature *> defaults = specialMembers(c, kDefaultCtor);
    for (const Signature *f : defaults)
        if (!f->implicit) return !f->deleted && constexprCtorSymbols_.count(f->symbol) != 0;
    if (defaults.empty())
        if (const std::vector<std::size_t> *set = overloadsOf(constructorKey(c->tag())))
            for (std::size_t i : *set)
                if (!functions_[i].implicit) return false;
    for (const Type::BaseSpec *b : c->directBases())
        if (b->isVirtual || !implicitDefaultIsConstexpr(b->type)) return false;
    for (const Member &m : c->members())
        if (ownMember(m) && memberInit_.count(c->tag() + "::" + m.name) == 0 &&
            !implicitDefaultIsConstexpr(m.type))
            return false;
    return true;
}

// [basic.types]/10 as C++11 has it - `void` is not one - with clang's reading of "at least one constexpr
// constructor": a default constructor that is trivial, or implicit and constexpr, counts.
bool Parser::literalType(const Type *t) const {
    if (t->isReference()) return true;
    const Type *c = elementOf(t);
    if (scalarType(c)) return true;
    if (!classType(c) || !trivialSpecial(c, kDestructor)) return false;
    if (!hasConstexprConstructor(c) && !isAggregateClass(c)) return false;
    for (const Member &m : c->members())
        if (ownMember(m) && (m.type->innermostElement()->isVolatile() || !literalType(m.type))) return false;
    for (const Type::BaseSpec *b : c->directBases())
        if (!literalType(b->type)) return false;
    return true;
}

bool Parser::hasConstexprConstructor(const Type *c) const {
    bool constexprCtor = hasTrivialDefaultConstructor(c) || implicitDefaultIsConstexpr(c);
    if (const std::vector<std::size_t> *set = overloadsOf(constructorKey(c->tag())))
        for (std::size_t i : *set) {
            const Signature &f = functions_[i];
            bool copyOrMove = false;
            for (SpecialMember s : { kCopyCtor, kMoveCtor })
                for (const Signature *g : specialMembers(c, s))
                    if (g == &f) copyOrMove = true;
            if (!copyOrMove && constexprCtorSymbols_.count(f.symbol) != 0) constexprCtor = true;
        }
    return constexprCtor;
}

// [dcl.constexpr]/3-4 where it is certain: a class no template made, whose destructor is not trivial, or which is
// neither an aggregate nor has a constexpr constructor (a constructor template might be one). Its members and
// bases are not asked, nor is a specialization, whose members are declared as they are used.
bool Parser::plainlyNotLiteral(const Type *t) const {
    if (t->isReference()) return false;
    const Type *c = elementOf(t);
    if (!classType(c) || !c->isComplete() || c->isSpecialization() || dependentType(c)) return false;
    if (!trivialSpecial(c, kDestructor)) return true;
    return !hasConstexprConstructor(c) && !isAggregateClass(c) && memberTemplates_.count(constructorKey(c->tag())) == 0;
}

// [meta.unary.prop]: no data member but zero-width bit-fields, no virtual function or base, no base that is not empty.
bool Parser::emptyClass(const Type *t) const {
    const Type *c = t->unqualified();
    if (!classType(c) || c->kind() == Kind::Union || c->hasVptr()) return false;
    for (const Member &m : c->members())
        if (!(m.isBitField() && m.width == 0)) return false;
    return true;
}

bool Parser::typeTraitHolds(int trait, const std::vector<const Type *> &ops, std::size_t pos) {
    const Type *t = ops[0];
    const Type *u = t->unqualified();
    bool quiet = false, trivial = false;
    switch (trait) {
    case IsClass:
        return classType(u) && u->kind() != Kind::Union;
    case IsUnion:
        return u->kind() == Kind::Union;
    case IsEnum:
        return u->isEnumeration();
    case IsPod:
        return standardLayout(t) && triviallyCopyable(t) && hasTrivialDefaultConstructor(t);
    case IsEmpty:
        return emptyClass(t);
    case IsPolymorphic:
        return classType(u) && u->polymorphic();
    case IsAbstract:
        return classType(u) && u->abstract();
    case IsFinal:
        return classType(u) && u->isFinal();
    case IsStandardLayout:
        return standardLayout(t);
    case IsTrivial:
        return triviallyCopyable(t) && hasTrivialDefaultConstructor(t);
    case IsTriviallyCopyable:
        return triviallyCopyable(t);
    case IsLiteralType:
        return literalType(t);
    case HasVirtualDestructor:
        return classType(u) && virtualDestructor(u);
    case IsDestructible:
        return traitDestructible(t);
    case IsNothrowDestructible: {
        if (!traitDestructible(t)) return false;
        const Type *c = t->isReference() ? nullptr : elementOf(t);
        // [class.dtor]/3: a destructor declared with no exception-specification has the one an implicit
        // destructor would have - `noexcept`, unless a base's or a member's may throw (1.77: the trait read
        // the written `noexcept` alone, and said false of every class with a destructor of its own).
        return c == nullptr || !classType(c) || !destructorMayThrow(c);
    }
    case IsTriviallyDestructible:
        return traitDestructible(t) && (t->isReference() || trivialSpecial(t, kDestructor));
    case IsBaseOf: {
        const Type *b = u, *d = ops[1]->unqualified();
        if (!classType(b) || !classType(d) || b->kind() == Kind::Union || d->kind() == Kind::Union)
            return false;
        return b == d || derivesFrom(d, b);
    }
    case IsConvertible: {
        const Type *to = ops[1];
        if (to->unqualified()->isVoid()) return u->isVoid();
        if (u->isVoid() || to->isArray() || to->isFunction()) return false;
        if (classType(to->unqualified()) && to->unqualified()->abstract()) return false;
        return traitWellFormed(kTraitConvert, to, std::vector<const Type *>(1, t), pos, &quiet);
    }
    case IsAssignable:
    case IsNothrowAssignable:
    case IsTriviallyAssignable: {
        if (u->isVoid() || ops[1]->unqualified()->isVoid()) return false;
        if (!traitWellFormed(kTraitAssign, nullptr, ops, pos, &quiet)) return false;
        if (trait == IsNothrowAssignable) return quiet;
        if (trait == IsAssignable) return true;
        const Type *to = (t->isReference() ? t->referent() : t)->unqualified();
        if (!classType(to)) return true;
        const Type *a = ops[1];
        const Type *from = a->isReference() ? a->referent() : a;
        if (from->unqualified() != to && !derivesFrom(from->unqualified(), to)) return false;
        const bool rvalue = !(a->isReference() && !a->isRValueReference());
        return trivialSpecial(to, rvalue && !from->isConst() ? kMoveAssign : kCopyAssign);
    }
    case IsConstructible:
    case IsNothrowConstructible:
    case IsTriviallyConstructible: {
        const std::vector<const Type *> args(ops.begin() + 1, ops.end());
        if (!traitConstructible(t, args, pos, &quiet, &trivial)) return false;
        return trait == IsConstructible || (trait == IsNothrowConstructible ? quiet : trivial);
    }
    }
    return false;
}
