#pragma once

#include <string>
#include <utility>
#include <vector>

enum class Kind {
    Void,
    // **`decltype(nullptr)`**, which is its own type and not a pointer.
    NullPtr,
    // bool sits inside the integer range and at the bottom of it, which is
    // what lets isInteger() stay a range check. Its position in this enum is
    // load-bearing: TypeTable builds one Type per value from Void to Function.
    Bool,
    Char, SChar, UChar,
    Short, UShort,
    // **wchar_t, a type of its own** ([basic.fundamental]/5): mangled `w`, an
    // overload apart from the integer it is represented as - which the target
    // names (Target::wcharType), and which its size, alignment and sign follow.
    WChar,
    Char16, Char32,
    Int, UInt,
    Long, ULong,
    LongLong, ULongLong,
    Float, Double, LongDouble,
    Struct, Union,
    Pointer, Array, Function,
    // After Function, and it matters.
    LValueRef,
    // **An rvalue reference, and it is the same machine as an lvalue one**.
    RValueRef,
    // **The expansion of a parameter pack**, `Ts...`, standing in a pattern where the pack's members stand in the substituted signature.
    PackExpansion,
    // **`auto`, standing where a type will be.**
    Deduced,
    // **A pointer to a data member**, `int S::*`, holding an *offset* and not an address.
    MemberPointer,
    // **A template parameter, and it is not a type anything is made of.**
    TemplateParam,
    // **A member type not known yet**, `T::type` - a pattern's only, Itanium spelling a return type from it.
    DependentMember,
    // **`decltype` of an expression over a template's parameters**, in a pattern only: Itanium
    // spells it `Dt...E` (an id-expression or a member access) or `DT...E`, the expression itself.
    DependentExpr
};

// A function type's ref-qualifier - [dcl.fct]/6 - and half of a member function's identity beside its const.
enum class RefQual : char { None, LValue, RValue };

class Target;

class Type;

// **An expression over a template's parameters, kept as a tree because the Itanium name spells it**
// - `sizeof(T) == 4` is `eqstT_Li4E`. Built by the pattern read, interned by TypeTable on `key`,
// meaning nothing to any backend. `kids` are the operands in the order the name writes them.
struct DepExpr {
    enum Op {
        Literal,            // `type` its type; `value` the integer, or the IEEE bits when `floating`
        TemplateParam,      // a non-type parameter, `index`
        FunctionParam,      // the `index`th function parameter, `fp_`
        SizeofType,         // `st <type>`
        SizeofExpr,         // `sz <expression>`
        SizeofPack,         // `sZ <template-param>`, the pack's parameter `index` (1.82)
        Operator,           // `code` and one, two or three operands
        Member,             // `dt <kid> <name>`
        Arrow,              // `pt <kid> <name>`
        Call,               // `cl <callee> <args> E`
        UnresolvedName,     // a function named by `name` and not looked up
        QualifiedParamName, // `sr <type> <name>`
        Cast,               // `cv <type> <kid>`, one operand
        CastList,           // `cv <type> _ <kids> E`, none or several
        StaticCast,         // `sc <type> <kid>`
        ExternalName,       // the one function a qualified name resolves to, `L <its symbol> E`; `name` the symbol
        QualifiedName,      // `sr <scope>... E <name> [I <targs> E]`: an overloaded or template function named qualified
        TypeTrait,          // `u <name> <targs> E`: a type-trait built-in of the parameters, `name` its spelling
        PackExpansion,      // `sp <kid>`: `f(std::forward<A>(a)...)`'s argument, one expression per member
        ScopedMember        // `sr <levels> E <name>`: `std::is_integral<T>::value`, `type` the dependent member
    };
    Op op = Literal;
    std::string code;
    const Type *type = nullptr;
    long long value = 0;
    bool floating = false;
    int index = -1;
    std::string name;
    std::vector<std::string> scope;
    std::vector<const Type *> targs;
    std::vector<const DepExpr *> kids;
    std::string key;    // the identity: equivalent expressions have one key and one node
    std::string text;   // what a diagnostic shows
};

// One template argument: a type, or a value of a type. `type` is the argument for
// a type argument and the *parameter's* type for a non-type one, which is what
// both ABIs write beside the value. Here because a specialization carries them.
struct TemplateArg {
    const Type *type = nullptr;
    bool isType = true;
    // A function template's non-type parameter declared with a type that depends on another parameter -
    // `T v` after `class T`: that declared type, standing for itself, which Itanium writes as `Tn<type>`
    // before the value. Null otherwise, and on every argument of a class template.
    const Type *declaredAs = nullptr;
    long long value = 0;
    // **A non-type argument that is a reference to a template parameter**, the
    // `N` in the pattern `V<N>`.
    bool isParam = false;
    int paramIndex = 0;
    // A value folded from an expression that names a parameter while a pattern is read, `V<N + M>`.
    bool dependentValue = false;
    // **A non-type argument that is an expression of the parameters**, in a pattern: `X <expr> E`.
    const DepExpr *expr = nullptr;
    // **A pack argument is a list, not a type.** Itanium spells one `J...E`
    // and Microsoft lists its members inline with `$$V` for an empty one, so
    // both need the members rather than anything standing for them.
    bool isPack = false;
    std::vector<const Type *> pack;
    // **A non-type pack's values**, one per member of `pack`, which then holds their type once each:
    // Itanium `JLi1ELi2EE`, Microsoft `$00$01` and `$S` for none. `type` is that element type.
    bool packIsValue = false;
    std::vector<long long> packValues;
    // **`Ns...` or `Ts...` in a pattern**: the expansion of pack parameter `expandsParam`, its one member that parameter itself - `JXspT_EE`, `JDpT_E`; -1 for a list.
    int expandsParam = -1;
    // **A template template argument**, `Holder<Box, int>`'s Box: `type` is the stand-in TypeTable::templateNameType makes,
    // or a Kind::TemplateParam where a pattern names an enclosing template template parameter.
    bool isTemplate = false;
    // **An address or member pointer as a non-type argument**: `form` is how
    // the ABIs wrap `entity` - 'p' pointer, 'c' gaining const, 'r' reference,
    // 'a' array, 'm' member at `offset`, 'n' null; 0 otherwise.
    char form = 0;
    std::string address;
    std::string entity;
    const Type *element = nullptr;
    int offset = 0;
    // What Itanium mangles in place of `entity`, so the scopes and parameter types in it are substitution
    // candidates as clang counts them: the name it was found under (`ns::nv`, `S::f`), a member's class,
    // and a function's type. Unset where `entity` is not a `_Z` name of its own.
    std::string name;
    const Type *scope = nullptr;
    const Type *function = nullptr;
};

int homogeneousFloatCount(const Type *t, Kind *elem);

bool containsX87(const Type *t, const Target &target);

void x87Parts(long double v, unsigned long long *significand, unsigned int *signExp);

int objectAlign(const Type *t, const Target &target);

// Who may name a member.
enum class Access { Public, Protected, Private };

struct Member {
    std::string name;
    const Type *type;
    int offset;
    int width = 0;
    int bitOffset = 0;
    Access access = Access::Public;
    // **[dcl.stc]/9: a `mutable` member is writable through a const object.**
    bool isMutable = false;
    // Set for a member living in a virtual base.
    const Type *inVirtualBase = nullptr;
    // **Which class declared it**, null for one the class wrote itself.
    const Type *declaredIn = nullptr;
    // In two base subobjects of the copying class (A25): unqualified, refused.
    bool ambiguous = false;
    // **[class.union]/5: the anonymous union this member came out of**, null for an ordinary one; the run sharing it is one slot.
    const Type *anonUnion = nullptr;

    bool isBitField() const { return width != 0; }
    bool continuesUnion(const Member *prev) const {
        return anonUnion != nullptr && prev != nullptr && prev->anonUnion == anonUnion;
    }
};

class Type {
public:
    explicit Type(Kind k) : kind_(k) {}
    Type(Kind k, const Type *pointee, long long length)
        : kind_(k), pointee_(pointee), length_(length) {}

    Kind kind() const { return kind_; }

    // Constness belongs to the type, not to the object that has it: `const char *`
    // and `char *` must be two types, overload resolution ranking between them and
    // the mangler spelling them apart. An array is const when its elements are.
    bool isConst() const {
        return const_ || (kind_ == Kind::Array && pointee_->isConst());
    }
    // And volatile the same way ([basic.type.qualifier]): `volatile int *` is not `int *`, and a read
    // through one is an access the program asked for.
    bool isVolatile() const {
        return volatile_ || (kind_ == Kind::Array && pointee_->isVolatile());
    }
    // Whether this is a cv-qualified copy of unqualified() - the question every "is const" test that
    // compared a type with its unqualified() was asking about the qualifier as a whole.
    bool isQualified() const { return unqual_ != nullptr; }
    const Type *unqualified() const { return unqual_ != nullptr ? unqual_ : this; }

    const Type *pointee() const { return pointee_; }
    long long length() const { return length_; }
    // **An array whose bound is a non-type template parameter**, the `[N]` of `T (&)[N]` read as
    // a pattern. length_ keeps -2 - index, below the -1 of an unknown bound, so arrayOf interns
    // it and withConst carries it like any other length.
    static long long parameterBound(int index) { return -2 - static_cast<long long>(index); }
    int boundParam() const {
        return kind_ == Kind::Array && length_ <= -2 ? static_cast<int>(-2 - length_) : -1;
    }

    // **An array whose bound is an expression over a pattern's parameters**, `char (*)[sizeof(T) == 4]` read as a
    // pattern: length_ is an unknown bound's -1, and the expression rides beside it.
    const DepExpr *dependentBound() const { return kind_ == Kind::Array ? expr_ : nullptr; }
    // The expression a `Kind::DependentExpr` is `decltype` of, and whether it is spelled `Dt`.
    const DepExpr *dependentExpr() const { return kind_ == Kind::DependentExpr ? expr_ : nullptr; }
    bool dependentIdExpression() const { return kind_ == Kind::DependentExpr && length_ == 1; }

    bool isPointer() const { return kind_ == Kind::Pointer; }

    // A reference is a pointer that has lost the right to be written or read as
    // one: it holds an address, occupies a pointer's storage, and every use goes
    // through it. The parser lowers it away, so no backend sees this kind.
    bool isReference() const {
        return kind_ == Kind::LValueRef || kind_ == Kind::RValueRef;
    }
    bool isRValueReference() const { return kind_ == Kind::RValueRef; }
    const Type *referent() const { return pointee_; }
    bool isArray() const { return kind_ == Kind::Array; }
    // The element under every array level, and whether two arrays agree at
    // every level. A reference to an array asks both: `const double (&)[2][3]`
    // takes a `double[2][3]`, the innermost element alone gaining const.
    const Type *innermostElement() const {
        const Type *t = this;
        while (t->kind_ == Kind::Array) t = t->pointee_;
        return t;
    }
    bool sameArrayShape(const Type *o) const;
    // [basic.types]/9 counts std::nullptr_t among the scalar types, which is what
    // lets `!nullptr` be written - a contextual conversion to bool. It does not
    // make `bool b = nullptr;`: [conv.bool] is direct-initialization only.
    bool isScalar() const {
        return isArithmetic() || isPointer() || isNullPtr();
    }

    bool isInteger() const {
        return kind_ >= Kind::Bool && kind_ <= Kind::ULongLong;
    }
    bool isFloating() const {
        return kind_ >= Kind::Float && kind_ <= Kind::LongDouble;
    }

    bool isX87(const Target &t) const;
    bool isArithmetic() const { return isInteger() || isFloating(); }
    bool isVoid() const { return kind_ == Kind::Void; }
    bool isNullPtr() const { return kind_ == Kind::NullPtr; }
    bool isBool() const { return kind_ == Kind::Bool; }
    bool isStructOrUnion() const { return kind_ == Kind::Struct || kind_ == Kind::Union; }
    bool isComplete() const {
        if (isVoid()) return false;
        if (isArray() && length_ < 0) return false;
        if (isStructOrUnion()) return cls().complete_;
        return true;
    }

    int size(const Target &t) const;
    int align(const Target &t) const;
    bool isSigned(const Target &t) const;

    int rank() const;

    const char *name() const;

    std::string describe() const;

    // **The tag as a debugger shows it** (1.29), cl's `std::basic_string<char>`, `geo::Box<int>::Lid`: the
    // namespace a specialization keeps beside its name (templateNamespace) put back in front, and the
    // `struct ` a key spells a class argument with taken out - displaySpelling, which does that to any key.
    std::string displayTag() const;
    static std::string displaySpelling(const std::string &key);
    // Said outright where the tag cannot tell: an enum written inside a class, which has no enclosing() to walk.
    void setDisplayTag(std::string t) { displayTag_ = std::move(t); }
    // **The class an enum is written inside** (1.29): the Microsoft name is scoped through it, as a nested class's is -
    // `Box<int>::Kind` is `W4Kind@?$Box@H@geo@@` - where the tag would put `Box<int>` into the symbol.
    const Type *enumScope() const { return cls().enumScope_; }
    void setEnumScope(const Type *c) { enumScope_ = c; }

    // **Every question about what a class *is* is asked of the unqualified type**, and this is the only place that says so.
    const Type &cls() const { return unqual_ != nullptr ? *unqual_ : *this; }

    const std::string &tag() const { return cls().tag_; }
    // [dcl.typedef]/9: an unnamed class first named by a typedef has that name for linkage.
    // `name` is the qualified key, `local` the typedef's own name; a class or a namespace around it is set too.
    void nameForLinkage(const std::string &name, const std::string &local, const Type *in, bool inNamespace) const {
        Type &c = const_cast<Type &>(cls());
        if (!c.tag_.empty() || (c.kind_ != Kind::Struct && c.kind_ != Kind::Union)) return;
        c.tag_ = name;
        c.local_ = local;
        if (in != nullptr && c.enclosing_ == nullptr) c.enclosing_ = in;
        if (inNamespace) c.inNamespace_ = true;
    }

    // **An unnamed class no typedef names for linkage**, as a template argument names it: cl's
    // `<unnamed-type-x>` after its first declarator - `scope` its class's or namespace's key, `::` ended -
    // and clang's `$_N` by `index`. Such a specialization has no linkage outside this unit.
    const std::string &unnamedName() const { return cls().unnamed_; }
    int unnamedIndex() const { return cls().unnamedIndex_; }
    void nameUnnamed(const std::string &scopedName, int index) const {
        Type &c = const_cast<Type &>(cls());
        if (!c.tag_.empty() || !c.unnamed_.empty()) return;
        c.unnamed_ = scopedName;
        c.unnamedIndex_ = index;
    }

    // `class X` and `struct X` build the same kind of type and differ only in the
    // default access - and in what a diagnostic should call it. "struct Account"
    // for something written as a class sends the reader after a missing line.
    bool declaredClass() const { return cls().isClass_; }
    void setDeclaredClass(bool c) { isClass_ = c; classKeySet_ = true; }
    // **A declaration records the keyword too, where no definition has.**
    void noteClassKey(bool c) { if (!classKeySet_) { isClass_ = c; } }

    // **A class written inside another one.** `tag()` is the qualified name, every
    // table being keyed by it; `localName()` is the component both ABIs spell, and
    // `enclosing()` is what Itanium's substitution table has to recognise.
    const std::string &localName() const {
        return cls().local_.empty() ? cls().tag_ : cls().local_;
    }
    void setLocalName(std::string n) { local_ = std::move(n); }

    // **A class written inside a namespace**, whose tag carries the namespaces as a
    // nested class's carries its classes - with nothing for `enclosing()` to point
    // at. The manglers ask it to tell "N::S" from a local class's "f::L".
    bool inNamespace() const { return cls().inNamespace_; }
    void setInNamespace() { inNamespace_ = true; }

    // **A class written inside a function**, carrying that function's linkage name,
    // which both ABIs wrap round this one. Until the type could answer this, such a
    // class as a template argument was spelled `7main::L` - a colon in a symbol.
    const std::string &localOwner() const { return cls().localOwner_; }
    void setLocalOwner(std::string o) { localOwner_ = std::move(o); }

    // A class made by instantiating a class template. The name and arguments
    // are kept because the tag - "Box<int,3>" - is the parser's key and not
    // anything a linker has ever seen.
    const std::string &enumTag() const { return tag_; }
    bool isEnumeration() const { return isInteger() && !tag_.empty(); }
    bool isSpecialization() const { return !cls().templateName_.empty() && !cls().templateNameOnly_; }
    const std::string &templateName() const { return cls().templateName_; }
    // **A stand-in for a template's name and nothing else** - what a template template parameter is bound
    // to, and what a template template argument carries; no object is ever of it. [temp.arg.template].
    bool isTemplateName() const { return cls().templateNameOnly_; }
    bool isAliasTemplate() const { return cls().aliasTemplate_; }
    void setTemplateNameOnly(std::string name, std::string ns, bool alias) {
        templateName_ = std::move(name); templateNamespace_ = std::move(ns);
        templateNameOnly_ = true; aliasTemplate_ = alias;
    }
    const std::vector<TemplateArg> &templateArgs() const { return cls().templateArgs_; }
    void setSpecialization(std::string name, std::vector<TemplateArg> args) {
        templateName_ = std::move(name);
        templateArgs_ = std::move(args);
    }
    // An enumeration's enumerators, unqualified and in order - for a debugger (CodeView's LF_ENUMERATE).
    const std::vector<std::pair<std::string, long long> > &enumerators() const { return cls().enumerators_; }
    void addEnumerator(const std::string &name, long long value) { enumerators_.push_back(std::make_pair(name, value)); }
    // `enum class` - [dcl.enum]/2: a distinct type whose enumerators are reached through it and
    // which converts to nothing implicitly; and whether its body has been read (an opaque one has not).
    bool isScopedEnumeration() const { return isEnumeration() && cls().scoped_; }
    void setScoped() { scoped_ = true; }
    // `enum E : short`, not scoped: [conv.prom]/4 promotes it to its underlying type as well as to int.
    bool hasFixedUnderlying() const { return isEnumeration() && cls().fixedUnderlying_; }
    void setFixedUnderlying() { fixedUnderlying_ = true; }
    bool enumDefined() const { return cls().enumDefined_; }
    void setEnumDefined() { enumDefined_ = true; }

    // **The namespace the template was declared in**, kept beside the bare name rather than folded
    // into it: `templateName_` is also the key `templates_` is looked up by, and that map is keyed
    // unqualified on purpose so `std::vector` finds `vector`.
    const std::string &templateNamespace() const {
        return cls().templateNamespace_;
    }
    void setTemplateNamespace(std::string ns) {
        templateNamespace_ = std::move(ns);
    }
    const Type *enclosing() const {
        return cls().enclosing_;
    }
    void setEnclosing(const Type *e) { enclosing_ = e; }
    // Who may name this class, when it is written inside another one. A
    // nested class is a member like any other and `private:` reaches it.
    Access nestedAccess() const {
        return cls().nestedAccess_;
    }
    void setNestedAccess(Access a) { nestedAccess_ = a; }

    // The one base class, or null: a base subobject sits at offset 0, so a derived
    // object's address is its base's. And whether any virtual function exists here
    // or in a base, which decides the layout before the members are placed.
    bool polymorphic() const { return cls().polymorphic_; }
    void setPolymorphic(bool p) { polymorphic_ = p; }

    // **A class with a pure virtual its own table has not filled in.**
    bool abstract() const { return cls().abstract_; }
    void setAbstract(bool a) { abstract_ = a; }

    // **`final` on the class head** - [class]/3 - checked where a base clause names it.
    bool isFinal() const { return cls().final_; }
    void setFinal() { final_ = true; }

    // **Whether copying this class is a function call rather than a move of bytes**,
    // which both platform ABIs make a question about how it is *passed*. Measured
    // with cl and clang; on the type, because the backends must agree with the parser.
    bool nonTrivialCopy() const { return cls().nonTrivialCopy_; }
    void setNonTrivialCopy(bool n) { nonTrivialCopy_ = n; }

    // **Whether this class has a destructor to run.** It decides how the class is
    // passed, and the ABIs disagree: Itanium by address whatever the size with the
    // caller destroying, Microsoft by size with the callee. Measured.
    bool hasDestructor() const { return cls().hasDestructor_; }
    void setHasDestructor(bool h) { hasDestructor_ = h; }

    // **Every base, with the offset it sits at.** The first is at 0 and a second is
    // not - measured, A at 0 and B at 4 - which is the whole difference multiple
    // inheritance makes, to a pointer and to the `this` a member expects.
    struct BaseSpec {
        const Type *type;
        int offset;
        Access access;
        // **One subobject however many paths reach it**, laid down by the
        bool isVirtual = false;
        // **Written in this class's base-clause**, not reached through one.
        bool direct = true;
        // Its place in that base-clause, -1 for one reached through another.
        int written = -1;
    };
    const std::vector<BaseSpec> &bases() const {
        return cls().bases_;
    }
    void addBase(const Type *b, int offset, Access how, bool isVirtual = false,
                 bool direct = true, int written = -1) {
        bases_.push_back(BaseSpec{ b, offset, how, isVirtual, direct, written });
    }
    // **The direct bases in the order the base-clause wrote them**, virtual
    // and non-virtual together: the Itanium ABI's every walk is that order.
    std::vector<const BaseSpec *> directBases() const {
        const std::vector<BaseSpec> &b = bases();
        std::vector<const BaseSpec *> out;
        for (std::size_t n = 0; n < b.size(); n++)
            for (std::size_t i = 0; i < b.size(); i++)
                if (b[i].direct && b[i].written == static_cast<int>(n))
                    out.push_back(&b[i]);
        return out;
    }
    // A class records every virtual base behind it as its own, so no walk goes down its bases.
    bool hasVirtualBase() const {
        const std::vector<BaseSpec> &b = bases();
        for (std::size_t i = 0; i < b.size(); i++)
            if (b[i].isVirtual) return true;
        return false;
    }
    // **A vptr is not the same question as `polymorphic`.**
    bool hasVptr() const { return polymorphic() || hasVirtualBase(); }
    // cl's model, which sizes a pointer to a member: 0 single, 1 multiple, 2 virtual, 3 incomplete.
    int microsoftInheritanceModel() const;
    // The size without the virtual bases - clang's `nvsize`. A base contributes
    // only this much, or the diamond holds three copies of V.
    int nvDataSize() const { return nvDataSize_ ? nvDataSize_ : dataSize(); }
    void setNvDataSize(int n) { nvDataSize_ = n; }
    // **cl's two layout flags**: whether the last base or class-typed member laid
    // carried a zero-sized subobject, and whether the first base did, or the class
    // itself is zero-sized. Where the two meet, the second base is pushed a byte.
    bool endsWithZeroSized() const { return cls().endsWithZeroSized_; }
    bool leadsWithZeroSized() const { return cls().leadsWithZeroSized_; }
    void setZeroSized(bool ends, bool leads) { endsWithZeroSized_ = ends; leadsWithZeroSized_ = leads; }
    // **Itanium's conflict list**: every empty subobject in this class, with its
    // offset, the class itself at 0 when it is empty. An empty base goes to 0
    // unless one of these of the same type is there, and then to the cursor.
    struct EmptyAt { const Type *type; int offset; };
    const std::vector<EmptyAt> &emptySubobjects() const { return cls().emptySubobjects_; }
    void setEmptySubobjects(const std::vector<EmptyAt> &e) { emptySubobjects_ = e; }

    // **Where this class's own vbptr sits, on the Microsoft ABI.**
    int vbptrOffset() const { return cls().vbptrOffset_; }
    void setVbptrOffset(int n) { vbptrOffset_ = n; }
    // The class whose vbptr this one uses - itself when it introduced the
    // pointer, or the base it came down from. Null where there is none.
    const Type *vbptrOwner() const { return cls().vbptrOwner_; }
    void setVbptrOwner(const Type *t) { vbptrOwner_ = t; }

    // **A class can hold more than one of them.**
    struct VbPtr {
        int offset;            // where it sits in this class
        const Type *owner;     // the class that introduced it: entry 0 is
                               // minus that class's own vbptr offset
        const Type *base;      // the direct base holding it, null where this
                               // class introduced the pointer itself
        const Type *via;       // the base of this class it was reached through, which
                               // names the table where `base` alone names two
    };
    const std::vector<VbPtr> &vbptrs() const { return cls().vbptrs_; }
    void addVbptr(int offset, const Type *owner, const Type *base, const Type *via = nullptr) {
        if (vbptrs_.empty()) { vbptrOffset_ = offset; vbptrOwner_ = owner; }
        vbptrs_.push_back(VbPtr{ offset, owner, base, via != nullptr ? via : base });
    }

    // **The Itanium ABI's primary base**: the first non-virtual base written
    // that carries a vptr, laid at offset 0 whatever its place in the
    // base-clause, so that this class's vptr *is* that base's.
    const Type *primaryBase() const { return cls().primaryBase_; }
    void setPrimaryBase(const Type *t) { primaryBase_ = t; }

    // A virtual base is recorded before the members and placed after them.
    void setBaseOffset(std::size_t i, int offset) { bases_[i].offset = offset; }
    // **A pointer the class introduces at offset 0 pushes every base down.**
    void shiftBaseOffsets(int by) {
        for (std::size_t i = 0; i < bases_.size(); i++) bases_[i].offset += by;
    }

    // The first base - the only one for most classes, and the only one that
    // needs no adjustment. Kept because most callers ask exactly that.
    const Type *base() const {
        const std::vector<BaseSpec> &b = bases();
        return b.empty() ? nullptr : b[0].type;
    }
    Access baseAccess() const {
        const std::vector<BaseSpec> &b = bases();
        return b.empty() ? Access::Public : b[0].access;
    }
    const std::vector<Member> &members() const {
        return cls().members_;
    }
    const Member *findMember(const std::string &name) const;

    // **A static data member is one object shared by the class, not one per
    // object.** It has no offset and takes no room, so it is kept apart from the
    // members: neither the size computation nor members() has to skip it.
    struct StaticMember {
        std::string name;
        const Type *type;
        Access access;
        std::string symbol;
        // A `static const` of integer type whose initialiser is written
        // inside the class is a constant and needs no definition at all - cl
        // emits no symbol for one and folds the value in. That value is here.
        bool folded = false;
        long long value = 0;
        bool isThreadLocal = false;   // `static thread_local`: one object per thread
    };
    const std::vector<StaticMember> &staticMembers() const {
        return cls().statics_;
    }
    void addStaticMember(StaticMember s) { statics_.push_back(std::move(s)); }
    // Searched up through the bases, the way a member function is.
    const StaticMember *findStaticMember(const std::string &name) const;

    void complete(std::vector<Member> members, int size, int align);

    // **Where this class's data actually ends, before the padding.** A base occupies
    // this rather than sizeof, so a derived class may sit in the base's tail
    // padding - what Itanium says and clang does. Equal to size() without one.
    int dataSize() const { return cls().dataSize_; }
    void setDataSize(int d) { dataSize_ = d; }

    const Type *returns() const { return pointee_; }
    const std::vector<const Type *> &params() const { return params_; }
    bool isVariadicFn() const { return variadic_; }
    // The ref-qualifier a function type was written with: `int () &` and `int ()` are two types.
    RefQual refQualifier() const { return refQual_; }
    bool isFunction() const { return kind_ == Kind::Function; }
    bool isMemberPointer() const {
        return cls().kind_ == Kind::MemberPointer;
    }
    // **A pointer to a member *function* wears the shape of a struct**, so that
    // every backend already knows how to copy, pass and return one. `kind_` is
    // Struct, and this flag tells describe() and the manglers what it really is.
    bool isMemberFunctionPointer() const {
        return cls().memberFn_;
    }
    void setMemberFunctionPointer() { memberFn_ = true; }
    bool isFunctionPointer() const {
        return kind_ == Kind::Pointer && pointee_ != nullptr && pointee_->isFunction();
    }
    std::string parameterList() const;
    // ` &`, ` &&` or nothing, as a function type's ref-qualifier is written after its parameters.
    const char *refQualifierSpelling() const {
        return refQual_ == RefQual::LValue ? " &" : refQual_ == RefQual::RValue ? " &&" : "";
    }
    // **The cv-qualifier-seq of a member function's type**, [dcl.fct]/6: `int (S::*)(int) const` is not
    // `int (S::*)(int)`. kThisConst and kThisVolatile, or 0.
    enum { kThisConst = 1, kThisVolatile = 2 };
    unsigned cvThis() const { return thisCv_; }
    const char *cvThisSpelling() const {
        return thisCv_ == 3 ? " const volatile" : thisCv_ == 2 ? " volatile" : thisCv_ == 1 ? " const" : "";
    }

private:
    friend class TypeTable;
    Kind kind_;

    // Set only on a qualified type, pointing at the unqualified one it was made
    // from. Everything that depends on state a struct gains later is asked of that
    // one, this copy having been taken before the struct was complete.
    const Type *unqual_ = nullptr;
    bool const_ = false;
    bool volatile_ = false;
    const DepExpr *expr_ = nullptr;

    const Type *pointee_ = nullptr;
    long long length_ = -1;

    std::vector<const Type *> params_;
    bool variadic_ = false;
    RefQual refQual_ = RefQual::None;
    unsigned thisCv_ = 0;

    std::string tag_;
    std::string local_;
    std::string localOwner_;
    std::string unnamed_;
    int unnamedIndex_ = -1;
    bool inNamespace_ = false;
    std::string displayTag_;
    const Type *enumScope_ = nullptr;
    // Set on a class that is a template specialization. `local_` is
    // "Box<int,3>" there, which no ABI writes: Itanium wants `3BoxIiLi3EE`
    // and Microsoft `?$Box@H$02@`, both built from these two.
    std::string templateName_;
    std::string templateNamespace_;
    std::vector<TemplateArg> templateArgs_;
    bool templateNameOnly_ = false;
    bool aliasTemplate_ = false;
    const Type *enclosing_ = nullptr;
    Access nestedAccess_ = Access::Public;
    bool isClass_ = false;
    bool classKeySet_ = false;
    bool memberFn_ = false;
    int dataSize_ = 0;
    bool polymorphic_ = false;
    bool abstract_ = false;
    bool final_ = false;
    bool nonTrivialCopy_ = false;
    bool hasDestructor_ = false;
    std::vector<BaseSpec> bases_;
    int nvDataSize_ = 0;
    bool endsWithZeroSized_ = false;
    bool leadsWithZeroSized_ = false;
    std::vector<EmptyAt> emptySubobjects_;
    const Type *primaryBase_ = nullptr;
    int vbptrOffset_ = -1;
    const Type *vbptrOwner_ = nullptr;
    std::vector<VbPtr> vbptrs_;
    std::vector<Member> members_;
    std::vector<StaticMember> statics_;
    std::vector<std::pair<std::string, long long> > enumerators_;
    bool scoped_ = false;
    bool fixedUnderlying_ = false;
    bool enumDefined_ = false;
    int size_ = 0;
    int align_ = 1;
    bool complete_ = false;
};

class TypeTable {
public:
    TypeTable();
    const Type *get(Kind k) const;

    const Type *withConst(const Type *t);
    const Type *withoutConst(const Type *t);
    const Type *withVolatile(const Type *t);
    const Type *withoutVolatile(const Type *t);
    // t with exactly this cv, whatever it had; and t with `of`'s cv added to its own.
    const Type *qualified(const Type *t, bool isConst, bool isVolatile);
    const Type *withCvOf(const Type *t, const Type *of);
    const Type *pointerTo(const Type *t);
    const Type *memberPointerTo(const Type *cls, const Type *member);
    // A pointer to member function made while its class was still being defined took the widest shape the
    // class could turn out to need; once the class is complete, each is given cl's shape for its model.
    void settleMemberFunctionPointers(const Type *cls, const Target &t);
    const Type *memberFunctionPointerTo(const Type *cls, const Type *fn,
                                        const Target &t);
    const Type *referenceTo(const Type *t);
    const Type *rvalueReferenceTo(const Type *t);
    const Type *arrayOf(const Type *t, long long length);
    const Type *dependentArrayOf(const Type *t, const DepExpr *bound);
    const Type *functionType(const Type *returns,
                             std::vector<const Type *> params, bool variadic,
                             RefQual ref = RefQual::None, unsigned cvThis = 0);
    // The Nth parameter of the template being mangled.
    const Type *templateParam(int index);
    // `auto`, before deduction has replaced it.
    const Type *deducedType();
    // `owner::name`, where owner is a pattern. Interned on the pair.
    const Type *dependentMember(const Type *owner, const std::string &name);
    const Type *packExpansion(const Type *of);
    // `decltype(e)` in a pattern; `id` for an id-expression or a member access (`Dt`).
    const Type *dependentExpr(const DepExpr *e, bool id);
    // One node per key: two equivalent expressions are one node, so two signatures are one type.
    const DepExpr *intern(DepExpr e);
    // A type's identity in a pattern, where describe() would call every parameter alike.
    static std::string patternKey(const Type *t);

    Type *structType(Kind kind, const std::string &tag);
    // The stand-in for a template named as a template template argument - `ns::NBox` as a name, interned.
    const Type *templateNameType(const std::string &name, const std::string &ns, bool alias, bool classKey);
    // **An enumeration, which is an `int` that remembers its name.**
    Type *enumType(const std::string &tag, Kind underlying = Kind::Int);
    Type *anonymousStruct(Kind kind);

    const Type *voidType() const   { return get(Kind::Void); }
    const Type *intType() const    { return get(Kind::Int); }
    const Type *doubleType() const { return get(Kind::Double); }
    const Type *charType() const   { return get(Kind::Char); }

private:
    std::vector<Type> types_;
    std::vector<Type *> derived_;
    std::vector<DepExpr *> exprs_;
};

class Target {
public:
    virtual ~Target() = default;

    virtual int sizeOf(Kind) const = 0;
    virtual int alignOf(Kind) const = 0;
    virtual bool plainCharIsSigned() const = 0;

    virtual Kind sizeType() const = 0;

    virtual Kind wcharType() const = 0;

    // Which ABI spells a C++ name, which is a property of the platform in the same way that the width of a long is.
    virtual bool microsoftNames() const = 0;
    // What the stack pointer is kept aligned to: 16 on the hosts, 8 on the C6000.
    virtual int stackAlign() const { return 16; }
    // Whether a frame can be realigned for a local that asks for more than that - the hosts' backends do.
    virtual bool realignsFrame() const { return true; }
    // **Thread storage**: whether the target has any (cl6x 8.2.2 has none - measured), and what
    // registers a thread_local's destructor: Itanium's __cxa_thread_atexit, dyld's _tlv_atexit,
    // the Microsoft CRT's __tlregdtor (a helper of no arguments, as atexit takes).
    virtual bool hasThreadStorage() const { return true; }
    virtual const char *threadAtexit() const { return "__cxa_thread_atexit"; }
    // Whether a word may sit at any address: the hosts' loads do, the C6000's LDW faults.
    virtual bool loadsUnaligned() const { return true; }
    // The ARM C++ ABI's array cookie: two size_t words, element size then count - arm64-darwin, measured.
    virtual bool armArrayCookie() const { return false; }
    // **How a cleanup pad hands the exception back**: `_Unwind_Resume(exception)`, or TI's `__cxa_end_cleanup()`.
    virtual bool resumeTakesException() const { return true; }
    // **Whether `__cxa_get_exception_ptr` exists** - TI's runtime copies from what `__cxa_begin_catch` returns.
    virtual bool hasGetExceptionPtr() const { return true; }
    // **Whether `__cxa_bad_typeid` exists**, so that `typeid(*p)` tests p. Every target's runtime has it: the C6747
    // said no until 1.74, when it could be asked - RTS6x has the routine since 1.3, and TI's rts6740 has it too
    // (CCS 7.4's cl6x 8.2.2 calls it, and the program catches std::bad_typeid).
    virtual bool hasBadTypeid() const { return true; }
    // **What `__cxa_begin_catch` returns for a thrown pointer**: the pointer's value on the hosts, the exception object's address on TI's runtime.
    virtual bool beginCatchReturnsObject() const { return false; }
    // **Who checks `throw(T...)`**: a filter in the frame's own pads on the hosts, the descriptor TI's personality reads.
    virtual bool specInTable() const { return false; }

    virtual const char *name() const = 0;
};
