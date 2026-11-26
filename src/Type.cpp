#include "Type.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

TypeTable::TypeTable() {
    for (int k = static_cast<int>(Kind::Void);
         k <= static_cast<int>(Kind::Function); k++)
        types_.push_back(Type(static_cast<Kind>(k)));
}

const Type *TypeTable::get(Kind k) const {
    return &types_[static_cast<std::size_t>(k)];
}

// clang's calculateInheritanceModel, measured against cl: virtual bases anywhere
// make it virtual; more than one base at any level of the single-base chain, or
// a class that adds the first vfptr over a base without one, make it multiple.
int Type::microsoftInheritanceModel() const {
    const Type *c = unqualified();
    if (!c->isComplete()) return 3;
    if (c->hasVirtualBase()) return 2;
    while (!c->bases().empty()) {
        if (c->bases().size() > 1) return 1;
        const Type *b = c->bases()[0].type->unqualified();
        if (c->polymorphic() && !b->polymorphic()) return 1;
        c = b;
    }
    return 0;
}

int Type::size(const Target &t) const {
    if (unqual_ != nullptr) return unqual_->size(t);
    // **A data member pointer is an offset**: Itanium's a ptrdiff_t, Microsoft's
    // an int, joined by a vbtable index under virtual inheritance - 4, 4, 8.
    if (kind_ == Kind::MemberPointer)
        return !t.microsoftNames() ? t.sizeOf(Kind::Pointer)
             : enclosing_ != nullptr && enclosing_->microsoftInheritanceModel() == 2 ? 8 : 4;
    if (isReference()) return pointee_->size(t);
    // **In `long long`, because the multiply itself was the bug.** Two int
    // operands overflowed for `static int a[600000000]` and the backend was
    // handed `.zero -1894967296`; the parser refuses it, so this only defines it.
    if (kind_ == Kind::Array) {
        const long long total =
            length_ * static_cast<long long>(pointee_->size(t));
        return total > 2147483647LL ? 2147483647 : static_cast<int>(total);
    }
    if (kind_ == Kind::Struct || kind_ == Kind::Union) return size_;
    return t.sizeOf(kind_);
}

int Type::align(const Target &t) const {
    if (unqual_ != nullptr) return unqual_->align(t);
    if (kind_ == Kind::MemberPointer) return t.microsoftNames() ? 4 : t.alignOf(Kind::Pointer);
    if (isReference()) return pointee_->align(t);
    if (kind_ == Kind::Array) return pointee_->align(t);
    if (kind_ == Kind::Struct || kind_ == Kind::Union) return align_;
    return t.alignOf(kind_);
}

std::string Type::parameterList() const {
    std::string s = "(";
    for (std::size_t i = 0; i < params_.size(); i++)
        s += (i ? ", " : "") + params_[i]->describe();
    if (variadic_) s += params_.empty() ? "..." : ", ...";
    if (params_.empty() && !variadic_) s += "void";
    return s + ")";
}

// `[2][3]` for a `double[2][3]`: every level's length, outermost first.
static std::string arrayDims(const Type *t) {
    std::string dims;
    for (; t->isArray(); t = t->pointee())
        dims += "[" + (t->dependentBound() != nullptr ? t->dependentBound()->text
                       : t->boundParam() >= 0
                           ? "template parameter " + std::to_string(t->boundParam())
                           : std::to_string(t->length())) + "]";
    return dims;
}

bool Type::sameArrayShape(const Type *o) const {
    const Type *a = this;
    const Type *b = o;
    while (a->isArray() && b->isArray()) {
        if (a->length() != b->length()) return false;
        a = a->pointee();
        b = b->pointee();
    }
    return !a->isArray() && !b->isArray();
}

std::string Type::describe() const {
    // A const pointer is written with the const behind the star, and every
    // other const in front of the type. Saying 'const char *' where the
    // program wrote 'char * const' would send a reader after the wrong error.
    if (unqual_ != nullptr) {
        const std::string cv = const_ && volatile_ ? "const volatile" : const_ ? "const" : "volatile";
        return kind_ == Kind::Pointer ? unqual_->describe() + " " + cv : cv + " " + unqual_->describe();
    }
    if (kind_ == Kind::Pointer && pointee_->isFunction())
        return pointee_->returns()->describe() + " (*)" + pointee_->parameterList();
    if (kind_ == Kind::Function)
        return pointee_->describe() + " " + parameterList() + cvThisSpelling() + refQualifierSpelling();
    if (kind_ == Kind::Pointer) return pointee_->describe() + " *";
    if (memberFn_)
        return pointee_->returns()->describe() + " (" +
               (enclosing_ != nullptr ? enclosing_->tag() : std::string("?")) +
               "::*)" + pointee_->parameterList() + pointee_->cvThisSpelling() +
               pointee_->refQualifierSpelling();
    if (kind_ == Kind::MemberPointer)
        return pointee_->describe() + " " +
               (enclosing_ != nullptr ? enclosing_->tag() : std::string("?")) +
               "::*";
    // A reference to an array is written round the name, the same way a
    // pointer to a function is: 'int (&)[3]', never 'int [3] &'.
    if (isReference() && pointee_->isArray())
        return pointee_->innermostElement()->describe() + (kind_ == Kind::RValueRef ? " (&&)" : " (&)") +
               arrayDims(pointee_);
    if (kind_ == Kind::LValueRef) return pointee_->describe() + " &";
    if (kind_ == Kind::RValueRef) return pointee_->describe() + " &&";
    // Outermost dimension first, as it is declared: `double [2][3]` for a
    // `double r[2][3]`. Recursing through the element wrote them backwards.
    if (kind_ == Kind::Array)
        return innermostElement()->describe() + " " + arrayDims(this);
    if (templateNameOnly_) return "template " + templateNamespace_ + templateName_;
    // An unnamed class is told apart by its first declarator where it has one, as cl tells it apart.
    const std::string unnamed = unnamed_.empty() ? std::string("<anonymous>") : unnamed_;
    if (kind_ == Kind::Struct)
        return std::string(isClass_ ? "class " : "struct ") +
               (tag_.empty() ? unnamed : tag_);
    if (kind_ == Kind::Union)  return "union "  + (tag_.empty() ? unnamed : tag_);
    if (kind_ == Kind::DependentExpr) return "decltype(" + expr_->text + ")";
    return name();
}

// A specialization's tag is the parser's key, `Box<int>`, its namespace apart
// (templateNamespace); a namespace or enclosing classes ride in the tag; the
// key writes `Box<struct Tally>`, cl's display name `Box<Tally>`.
std::string Type::displayTag() const {
    if (!cls().displayTag_.empty()) return displaySpelling(cls().displayTag_);
    const Type *root = &cls();
    while (root->enclosing() != nullptr) root = root->enclosing();
    const std::string ns = root->isSpecialization() && !root->inNamespace() ? root->templateNamespace() : std::string();
    return displaySpelling(ns + tag());
}

std::string Type::displaySpelling(const std::string &key) {
    static const char *const words[] = { "struct ", "class ", "union " };
    std::string out;
    for (std::size_t i = 0; i < key.size();) {
        bool dropped = false;
        if (i == 0 || key[i - 1] == '<' || key[i - 1] == ',' || key[i - 1] == ' ')
            for (std::size_t w = 0; w < 3 && !dropped; w++) {
                const std::size_t n = std::strlen(words[w]);
                if (key.compare(i, n, words[w]) == 0) { i += n; dropped = true; }
            }
        if (!dropped) out += key[i++];
    }
    return out;
}

// Every one of these interning loops skips the qualified types, and must. A
// `char * const` is a Kind::Pointer whose pointee is char, so a pointerTo() that
// did not skip it would hand back the const one for every `char *` in the file.
const Type *TypeTable::functionType(const Type *returns,
                                    std::vector<const Type *> params,
                                    bool variadic, RefQual ref, unsigned cvThis) {
    for (Type *d : derived_)
        if (d->unqual_ == nullptr && d->kind() == Kind::Function && d->pointee() == returns &&
            d->variadic_ == variadic && d->params_ == params && d->refQual_ == ref &&
            d->thisCv_ == cvThis)
            return d;
    Type *t = new Type(Kind::Function, returns, -1);
    t->params_ = std::move(params);
    t->variadic_ = variadic;
    t->refQual_ = ref;
    t->thisCv_ = cvThis;
    derived_.push_back(t);
    return derived_.back();
}

// **One type per cv-qualification of an unqualified one**: `const int`, `volatile int` and `const
// volatile int` each point their unqual_ at `int`, so unqualified() strips both at once.
const Type *TypeTable::qualified(const Type *t, bool isConst, bool isVolatile) {
    const Type *u = t->unqualified();
    if (!isConst && !isVolatile) return u;
    if (u->isReference()) return u;         // [dcl.ref]/1, as withConst
    for (Type *d : derived_)
        if (d->unqual_ == u && d->const_ == isConst && d->volatile_ == isVolatile) return d;
    Type *c = new Type(*u);
    c->const_ = isConst;
    c->volatile_ = isVolatile;
    c->unqual_ = u;
    derived_.push_back(c);
    return c;
}

const Type *TypeTable::withConst(const Type *t) {
    if (t->isConst()) return t;

    // cv on an array is cv on its elements - [basic.type.qualifier]/3 - so
    // 'const A' where A is int[3] must reach the int, or a write through a
    // subscript would not be refused.
    if (t->isArray())
        return arrayOf(withConst(t->pointee()), t->length());

    // A function type cannot be const-qualified.
    if (t->isFunction()) return t;
    // Nor a reference: [dcl.ref]/1 - cv brought to one by a typedef or a template argument is ignored, so
    // `const T` with T = int & is int & itself, the type add_const<int &> must name (until 1.75 it was a type
    // of its own that behaved as the reference and compared unequal to it).
    if (t->isReference()) return t;
    return qualified(t, true, t->isVolatile());
}

const Type *TypeTable::withoutConst(const Type *t) {
    if (t->isArray() && t->pointee()->isConst())
        return arrayOf(withoutConst(t->pointee()), t->length());
    if (t->isArray()) return t;
    return qualified(t, false, t->isVolatile());
}

const Type *TypeTable::withVolatile(const Type *t) {
    if (t->isVolatile()) return t;
    if (t->isArray())
        return arrayOf(withVolatile(t->pointee()), t->length());
    if (t->isFunction()) return t;
    if (t->isReference()) return t;
    return qualified(t, t->isConst(), true);
}

const Type *TypeTable::withoutVolatile(const Type *t) {
    if (t->isArray() && t->pointee()->isVolatile())
        return arrayOf(withoutVolatile(t->pointee()), t->length());
    if (t->isArray()) return t;
    return qualified(t, t->isConst(), false);
}

const Type *TypeTable::withCvOf(const Type *t, const Type *of) {
    const Type *r = of->isConst() ? withConst(t) : t;
    return of->isVolatile() ? withVolatile(r) : r;
}

const Type *TypeTable::pointerTo(const Type *t) {
    for (Type *d : derived_)
        if (d->unqual_ == nullptr && d->kind() == Kind::Pointer && d->pointee() == t)
            return d;
    derived_.push_back(new Type(Kind::Pointer, t, -1));
    return derived_.back();
}

// Interned on the pair, so two mentions of `int S::*` are one Type and every
// table keyed by pointer sees the repeat they are.
const Type *TypeTable::memberPointerTo(const Type *cls, const Type *member) {
    for (Type *d : derived_)
        if (d->unqual_ == nullptr && d->pointee() == member && d->enclosing() == cls &&
            (d->kind() == Kind::MemberPointer || d->isMemberFunctionPointer()))
            return d;
    Type *made = new Type(Kind::MemberPointer, member, -1);
    made->setEnclosing(cls);
    derived_.push_back(made);
    return derived_.back();
}

// A pointer to a member *function*, built with the shape of a struct so every
// backend already knows how to copy, pass and return one. Its members are what
// the ABI keeps: a code address, and on Itanium a `this` adjustment beside it.
const Type *TypeTable::memberFunctionPointerTo(const Type *cls, const Type *fn,
                                               const Target &t) {
    // Itanium's pair is two pointer-widths, the adjustment a ptrdiff_t. Microsoft's
    // is by cl's model: a code pointer; an int `this` adjustment after it under
    // multiple inheritance; a vbtable index after that under virtual - 8, 16, 16.
    const bool microsoft = t.microsoftNames();
    const int word = t.sizeOf(Kind::Pointer);
    const int model = microsoft ? cls->microsoftInheritanceModel() : 0;
    for (Type *d : derived_)
        if (d->unqual_ == nullptr && d->isMemberFunctionPointer() &&
            d->pointee() == fn && d->enclosing() == cls)
            return d;
    Type *made = new Type(Kind::Struct, fn, -1);
    made->setEnclosing(cls);
    made->setMemberFunctionPointer();
    const Type *code = pointerTo(get(Kind::Void));
    std::vector<Member> ms;
    ms.push_back(Member{ "$fn", code, 0, 0, 0, Access::Public });
    if (!microsoft)
        ms.push_back(Member{ "$adj", get(word == 8 ? Kind::LongLong : Kind::Int),
                             word, 0, 0, Access::Public });
    else if (model >= 1)
        ms.push_back(Member{ "$adj", get(Kind::Int), word, 0, 0, Access::Public });
    if (microsoft && model == 2)
        ms.push_back(Member{ "$vbi", get(Kind::Int), word + 4, 0, 0, Access::Public });
    made->complete(ms, !microsoft ? 2 * word : model == 0 ? word : 2 * word, word);
    derived_.push_back(made);
    return derived_.back();
}

void TypeTable::settleMemberFunctionPointers(const Type *cls, const Target &t) {
    if (!t.microsoftNames()) return;
    const int word = t.sizeOf(Kind::Pointer);
    const int model = cls->microsoftInheritanceModel();
    for (Type *d : derived_) {
        if (d->unqual_ != nullptr || !d->isMemberFunctionPointer() || d->enclosing() != cls) continue;
        const Type *code = pointerTo(get(Kind::Void));
        std::vector<Member> ms;
        ms.push_back(Member{ "$fn", code, 0, 0, 0, Access::Public });
        if (model >= 1) ms.push_back(Member{ "$adj", get(Kind::Int), word, 0, 0, Access::Public });
        if (model == 2) ms.push_back(Member{ "$vbi", get(Kind::Int), word + 4, 0, 0, Access::Public });
        d->complete(ms, model == 0 ? word : 2 * word, word);
    }
}

const Type *TypeTable::referenceTo(const Type *t) {
    for (Type *d : derived_)
        if (d->unqual_ == nullptr && d->kind() == Kind::LValueRef && d->pointee() == t)
            return d;
    derived_.push_back(new Type(Kind::LValueRef, t, -1));
    return derived_.back();
}

// The index is kept in length_, which nothing else on a template parameter uses.
// Interned like every other derived type, and the loop skips a qualified copy
// for the same reason every loop here does, or `const T` comes back for `T`.
const Type *TypeTable::templateParam(int index) {
    for (Type *d : derived_)
        if (d->unqual_ == nullptr && d->kind() == Kind::TemplateParam &&
            d->length() == index)
            return d;
    derived_.push_back(new Type(Kind::TemplateParam, nullptr, index));
    return derived_.back();
}

// The owner is kept in pointee_ and the member's name in tag_, neither of
// which a dependent member uses for anything else.
const Type *TypeTable::dependentMember(const Type *owner,
                                       const std::string &name) {
    for (Type *d : derived_)
        if (d->unqual_ == nullptr && d->kind() == Kind::DependentMember &&
            d->pointee() == owner && d->tag() == name)
            return d;
    derived_.push_back(new Type(Kind::DependentMember, owner, -1));
    derived_.back()->tag_ = name;
    return derived_.back();
}

// The tree kept in expr_, `Dt` against `DT` in length_ (1 and 0), interned on the pair.
const Type *TypeTable::dependentExpr(const DepExpr *e, bool id) {
    for (Type *d : derived_)
        if (d->unqual_ == nullptr && d->kind() == Kind::DependentExpr && d->expr_ == e &&
            d->length() == (id ? 1 : 0))
            return d;
    derived_.push_back(new Type(Kind::DependentExpr, nullptr, id ? 1 : 0));
    derived_.back()->expr_ = e;
    return derived_.back();
}

// [temp.over.link]/5: equivalent expressions are the same tokens up to the parameters' names, so
// the key spells the tree with every parameter by its index and every type by patternKey.
const DepExpr *TypeTable::intern(DepExpr e) {
    std::string key = std::to_string(static_cast<int>(e.op)) + "{" + e.code + "|" +
                      (e.type != nullptr ? patternKey(e.type) : std::string()) + "|" +
                      std::to_string(e.value) + (e.floating ? "f" : "") + "|" +
                      std::to_string(e.index) + "|" + e.name;
    for (const std::string &s : e.scope) key += "|::" + s;
    for (const Type *t : e.targs) key += "|<" + patternKey(t) + ">";
    for (const DepExpr *k : e.kids) key += "|" + k->key;
    e.key = key + "}";
    for (DepExpr *d : exprs_)
        if (d->key == e.key) return d;
    exprs_.push_back(new DepExpr(e));
    return exprs_.back();
}

std::string TypeTable::patternKey(const Type *t) {
    if (t == nullptr) return "?";
    if (t->isQualified())
        return std::string(t->isConst() ? "const " : "") + (t->isVolatile() ? "volatile " : "") +
               patternKey(t->unqualified());
    switch (t->kind()) {
    case Kind::TemplateParam:   return "$T" + std::to_string(t->length());
    case Kind::DependentExpr:   return (t->length() == 1 ? "$Dt" : "$DT") + t->dependentExpr()->key;
    case Kind::DependentMember: return patternKey(t->pointee()) + "::" + t->tag();
    case Kind::PackExpansion:   return patternKey(t->pointee()) + "...";
    case Kind::Pointer:         return patternKey(t->pointee()) + " *";
    case Kind::LValueRef:       return patternKey(t->pointee()) + " &";
    case Kind::RValueRef:       return patternKey(t->pointee()) + " &&";
    case Kind::Array:
        return patternKey(t->pointee()) + " [" +
               (t->dependentBound() != nullptr ? t->dependentBound()->key : std::to_string(t->length())) + "]";
    case Kind::Function: {
        std::string k = patternKey(t->returns()) + " (";
        for (const Type *p : t->params()) k += patternKey(p) + ",";
        return k + (t->isVariadicFn() ? "...)" : ")");
    }
    default:                    return t->describe();
    }
}

const Type *TypeTable::packExpansion(const Type *of) {
    for (Type *d : derived_)
        if (d->unqual_ == nullptr && d->kind() == Kind::PackExpansion && d->pointee() == of)
            return d;
    derived_.push_back(new Type(Kind::PackExpansion, of, -1));
    return derived_.back();
}

const Type *TypeTable::deducedType() {
    for (Type *d : derived_)
        if (d->unqual_ == nullptr && d->kind() == Kind::Deduced) return d;
    derived_.push_back(new Type(Kind::Deduced, nullptr, -1));
    return derived_.back();
}

const Type *TypeTable::rvalueReferenceTo(const Type *t) {
    for (Type *d : derived_)
        if (d->unqual_ == nullptr && d->kind() == Kind::RValueRef && d->pointee() == t)
            return d;
    derived_.push_back(new Type(Kind::RValueRef, t, -1));
    return derived_.back();
}

const Type *TypeTable::arrayOf(const Type *t, long long length) {
    for (Type *d : derived_)
        if (d->unqual_ == nullptr && d->kind() == Kind::Array && d->pointee() == t &&
            d->length() == length && d->expr_ == nullptr)
            return d;
    derived_.push_back(new Type(Kind::Array, t, length));
    return derived_.back();
}

const Type *TypeTable::dependentArrayOf(const Type *t, const DepExpr *bound) {
    for (Type *d : derived_)
        if (d->unqual_ == nullptr && d->kind() == Kind::Array && d->pointee() == t && d->expr_ == bound)
            return d;
    derived_.push_back(new Type(Kind::Array, t, -1));
    derived_.back()->expr_ = bound;
    return derived_.back();
}

const Member *Type::findMember(const std::string &name) const {
    // **members(), not members_** (a qualified copy has none); **backwards**,
    // as a derived member hides a base's; **the class's own first**, since a
    // virtual base is laid after them.
    const std::vector<Member> &all = members();
    for (std::size_t i = all.size(); i > 0; i--)
        if (all[i - 1].name == name && all[i - 1].declaredIn == nullptr) return &all[i - 1];
    for (std::size_t i = all.size(); i > 0; i--)
        if (all[i - 1].name == name) return &all[i - 1];
    return nullptr;
}

const Type::StaticMember *Type::findStaticMember(const std::string &name) const {
    for (const StaticMember &s : staticMembers())
        if (s.name == name) return &s;
    // Not its own, so a base's - searched the way a member function is,
    // because a static member lives under a name and not at an offset, and a
    // derived class names its base's without anything being copied down.
    for (const BaseSpec &b : bases())
        if (const StaticMember *s = b.type->findStaticMember(name)) return s;
    return nullptr;
}

void Type::complete(std::vector<Member> members, int size, int align) {
    members_ = std::move(members);
    size_ = size;
    align_ = align;
    complete_ = true;
}

Type *TypeTable::structType(Kind kind, const std::string &tag) {
    for (Type *d : derived_)
        if (d->unqual_ == nullptr && d->kind() == kind && d->tag_ == tag) return d;
    Type *t = new Type(kind);
    t->tag_ = tag;
    derived_.push_back(t);
    return t;
}

// The tag carries a `$` so it can collide with nothing written; the manglers spell the name beside it.
const Type *TypeTable::templateNameType(const std::string &name, const std::string &ns, bool alias,
                                        bool classKey) {
    Type *t = structType(Kind::Struct, "$template:" + ns + name);
    if (!t->isTemplateName()) {
        t->setTemplateNameOnly(name, ns, alias);
        t->setDeclaredClass(classKey);
    }
    return t;
}

Type *TypeTable::enumType(const std::string &tag, Kind underlying) {
    for (Type *d : derived_)
        if (d->unqual_ == nullptr && d->isEnumeration() && d->tag_ == tag) return d;
    Type *t = new Type(underlying);
    t->tag_ = tag;
    derived_.push_back(t);
    return t;
}

Type *TypeTable::anonymousStruct(Kind kind) {
    Type *t = new Type(kind);
    derived_.push_back(t);
    return t;
}

bool Type::isSigned(const Target &t) const {
    switch (kind_) {
    case Kind::Char:      return t.plainCharIsSigned();
    case Kind::WChar:     return t.wcharType() != Kind::UShort && t.wcharType() != Kind::UInt;
    case Kind::SChar:
    case Kind::Short:
    case Kind::Int:
    case Kind::Long:
    case Kind::LongLong:  return true;
    default:              return false;
    }
}

int Type::rank() const {
    switch (kind_) {
    // bool shares char's rank rather than sitting below it, as [conv.rank]
    // would have it. Nothing can observe the difference: integer promotion
    // turns both into int before rank is ever consulted.
    case Kind::Bool:                                           return 1;
    case Kind::Char: case Kind::SChar: case Kind::UChar:       return 1;
    case Kind::Short: case Kind::UShort:                       return 2;
    case Kind::WChar:                                          return 2;   // promotes to int whatever its width
    case Kind::Char16:                                         return 2;
    case Kind::Char32:                                         return 3;   // promotes to unsigned int
    case Kind::Int: case Kind::UInt:                           return 3;
    case Kind::Long: case Kind::ULong:                         return 4;
    case Kind::LongLong: case Kind::ULongLong:                 return 5;
    case Kind::Float:                                          return 6;
    case Kind::Double:                                         return 7;
    case Kind::LongDouble:                                     return 8;
    default:                                                   return 0;
    }
}

bool Type::isX87(const Target &t) const {
    return kind_ == Kind::LongDouble && t.sizeOf(Kind::LongDouble) > 8;
}

void x87Parts(long double v, unsigned long long *significand, unsigned int *signExp) {

    bool neg = std::signbit(v);
    if (neg) v = -v;

    unsigned long long sig = 0;
    unsigned int expField = 0;

    if (std::isnan(v)) {
        expField = 0x7fff;
        sig = 0xc000000000000000ULL;
    } else if (std::isinf(v)) {
        expField = 0x7fff;
        sig = 0x8000000000000000ULL;
    } else if (v != 0) {
        int e = 0;
        long double m = std::frexp(v, &e);
        sig = static_cast<unsigned long long>(std::ldexp(m, 64));
        expField = static_cast<unsigned int>(e - 1 + 16383) & 0x7fffu;
    }

    *significand = sig;
    *signExp = (neg ? 0x8000u : 0u) | expField;
}

int objectAlign(const Type *t, const Target &target) {
    int a = t->align(target);
    if (t->size(target) >= 16 && a < 16) a = 16;
    return a;
}

bool containsX87(const Type *t, const Target &target) {
    if (t == nullptr) return false;
    if (t->isX87(target)) return true;
    if (t->isArray()) return containsX87(t->pointee(), target);
    if (t->isStructOrUnion())
        for (const Member &m : t->members())
            if (containsX87(m.type, target)) return true;
    return false;
}

const char *Type::name() const {
    switch (kind_) {
    case Kind::Void:      return "void";
    // What the standard calls it, and what clang prints in a diagnostic. The
    // *name* `std::nullptr_t` needs <cstddef>, which there is none of here;
    // `decltype(nullptr)` is how a program spells the type.
    case Kind::NullPtr:   return "std::nullptr_t";
    case Kind::Bool:      return "bool";
    case Kind::Char:      return "char";
    case Kind::SChar:     return "signed char";
    case Kind::UChar:     return "unsigned char";
    case Kind::Short:     return "short";
    case Kind::UShort:    return "unsigned short";
    case Kind::WChar:     return "wchar_t";
    case Kind::Char16:    return "char16_t";
    case Kind::Char32:    return "char32_t";
    case Kind::Int:       return "int";
    case Kind::UInt:      return "unsigned int";
    case Kind::Long:      return "long";
    case Kind::ULong:     return "unsigned long";
    case Kind::LongLong:  return "long long";
    case Kind::ULongLong: return "unsigned long long";
    case Kind::Float:     return "float";
    case Kind::Double:    return "double";
    case Kind::LongDouble: return "long double";
    case Kind::Struct:    return "struct";
    case Kind::Union:     return "union";
    case Kind::Pointer:   return "pointer";
    case Kind::Array:     return "array";
    case Kind::Function:  return "function";
    case Kind::MemberPointer: return "pointer to member";
    case Kind::LValueRef: return "reference";
    case Kind::RValueRef: return "rvalue reference";
    case Kind::Deduced:       return "auto";
    case Kind::TemplateParam: return "template parameter";
    case Kind::DependentMember: return "dependent member type";
    case Kind::DependentExpr: return "decltype of a dependent expression";
    case Kind::PackExpansion: return "parameter pack expansion";
    }
    return "?";
}

static Kind hfaElem(const Type *t) {
    return t->kind() == Kind::LongDouble ? Kind::Double : t->kind();
}

static int hfaWalk(const Type *t, Kind *elem, bool *set) {
    if (t == nullptr) return 0;
    if (t->isFloating()) {
        if (!*set) { *elem = hfaElem(t); *set = true; }
        else if (*elem != hfaElem(t)) return 0;
        return 1;
    }
    if (t->kind() == Kind::Array) {
        if (t->length() <= 0) return 0;
        int one = hfaWalk(t->pointee(), elem, set);
        if (one == 0) return 0;
        long long total = one * t->length();
        return total > 4 ? 5 : static_cast<int>(total);
    }
    if (t->isStructOrUnion()) {

        bool isUnion = t->kind() == Kind::Union;
        int total = 0;
        // An anonymous union's members share one slot: the run counts its widest, as a union does.
        const Member *prev = nullptr;
        int runMax = 0;
        for (const Member &m : t->members()) {
            if (m.isBitField()) return 0;
            int one = hfaWalk(m.type, elem, set);
            if (one == 0) return 0;
            if (isUnion) total = one > total ? one : total;
            else if (m.continuesUnion(prev)) { if (one > runMax) { total += one - runMax; runMax = one; } }
            else { total += one; runMax = one; }
            prev = &m;
            if (total > 4) return 5;
        }
        return total;
    }
    return 0;
}

int homogeneousFloatCount(const Type *t, Kind *elem) {
    if (t == nullptr || !t->isStructOrUnion()) return 0;
    if (t->members().empty()) return 0;
    Kind k = Kind::Double;
    bool set = false;
    int n = hfaWalk(t, &k, &set);
    if (!set || n < 1 || n > 4) return 0;
    *elem = k;
    return n;
}
