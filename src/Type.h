#pragma once

#include <string>
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
    // **A member type of something that is not known yet** - `T::type`, or
    // `Value<T>::type`. Like TemplateParam it exists only in a pattern, because
    // Itanium spells a function template's return type from the pattern.
    DependentMember
};

class Target;

class Type;

// One template argument: a type, or a value of a type. `type` is the argument for
// a type argument and the *parameter's* type for a non-type one, which is what
// both ABIs write beside the value. Here because a specialization carries them.
struct TemplateArg {
    const Type *type = nullptr;
    bool isType = true;
    long long value = 0;
    // **A non-type argument that is a reference to a template parameter**, the
    // `N` in the pattern `V<N>`.
    bool isParam = false;
    int paramIndex = 0;
    // **A pack argument is a list, not a type.** Itanium spells one `J...E`
    // and Microsoft lists its members inline with `$$V` for an empty one, so
    // both need the members rather than anything standing for them.
    bool isPack = false;
    std::vector<const Type *> pack;
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

    bool isBitField() const { return width != 0; }
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
    const Type *unqualified() const { return unqual_ != nullptr ? unqual_ : this; }

    const Type *pointee() const { return pointee_; }
    long long length() const { return length_; }

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

    // **Every question about what a class *is* is asked of the unqualified type**, and this is the only place that says so.
    const Type &cls() const { return unqual_ != nullptr ? *unqual_ : *this; }

    const std::string &tag() const { return cls().tag_; }

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
    bool isEnumeration() const { return kind_ == Kind::Int && !tag_.empty(); }
    bool isSpecialization() const { return !cls().templateName_.empty(); }
    const std::string &templateName() const { return cls().templateName_; }
    const std::vector<TemplateArg> &templateArgs() const { return cls().templateArgs_; }
    void setSpecialization(std::string name, std::vector<TemplateArg> args) {
        templateName_ = std::move(name);
        templateArgs_ = std::move(args);
    }
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
        // **Written in this class's base-clause, rather than reached through
        // one.**
        bool direct = true;
    };
    const std::vector<BaseSpec> &bases() const {
        return cls().bases_;
    }
    void addBase(const Type *b, int offset, Access how, bool isVirtual = false,
                 bool direct = true) {
        bases_.push_back(BaseSpec{ b, offset, how, isVirtual, direct });
    }
    bool hasVirtualBase() const {
        const std::vector<BaseSpec> &b = bases();
        for (std::size_t i = 0; i < b.size(); i++)
            if (b[i].isVirtual || b[i].type->hasVirtualBase()) return true;
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
    };
    const std::vector<VbPtr> &vbptrs() const { return cls().vbptrs_; }
    void addVbptr(int offset, const Type *owner, const Type *base) {
        if (vbptrs_.empty()) { vbptrOffset_ = offset; vbptrOwner_ = owner; }
        vbptrs_.push_back(VbPtr{ offset, owner, base });
    }

    // **The Itanium ABI's primary base**: the first non-virtual base written
    // that carries a vptr, laid at offset 0 whatever its place in the
    // base-clause, so that this class's vptr *is* that base's.
    const Type *primaryBase() const { return cls().primaryBase_; }
    void setPrimaryBase(const Type *t) { primaryBase_ = t; }

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

private:
    friend class TypeTable;
    Kind kind_;

    // Set only on a qualified type, pointing at the unqualified one it was made
    // from. Everything that depends on state a struct gains later is asked of that
    // one, this copy having been taken before the struct was complete.
    const Type *unqual_ = nullptr;
    bool const_ = false;

    const Type *pointee_ = nullptr;
    long long length_ = -1;

    std::vector<const Type *> params_;
    bool variadic_ = false;

    std::string tag_;
    std::string local_;
    std::string localOwner_;
    bool inNamespace_ = false;
    // Set on a class that is a template specialization. `local_` is
    // "Box<int,3>" there, which no ABI writes: Itanium wants `3BoxIiLi3EE`
    // and Microsoft `?$Box@H$02@`, both built from these two.
    std::string templateName_;
    std::string templateNamespace_;
    std::vector<TemplateArg> templateArgs_;
    const Type *enclosing_ = nullptr;
    Access nestedAccess_ = Access::Public;
    bool isClass_ = false;
    bool classKeySet_ = false;
    bool memberFn_ = false;
    int dataSize_ = 0;
    bool polymorphic_ = false;
    bool abstract_ = false;
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
    const Type *pointerTo(const Type *t);
    const Type *memberPointerTo(const Type *cls, const Type *member);
    const Type *memberFunctionPointerTo(const Type *cls, const Type *fn,
                                        const Target &t);
    const Type *referenceTo(const Type *t);
    const Type *rvalueReferenceTo(const Type *t);
    const Type *arrayOf(const Type *t, long long length);
    const Type *functionType(const Type *returns,
                             std::vector<const Type *> params, bool variadic);
    // The Nth parameter of the template being mangled.
    const Type *templateParam(int index);
    // `auto`, before deduction has replaced it.
    const Type *deducedType();
    // `owner::name`, where owner is a pattern. Interned on the pair.
    const Type *dependentMember(const Type *owner, const std::string &name);
    const Type *packExpansion(const Type *of);

    Type *structType(Kind kind, const std::string &tag);
    // **An enumeration, which is an `int` that remembers its name.**
    Type *enumType(const std::string &tag);
    Type *anonymousStruct(Kind kind);

    const Type *voidType() const   { return get(Kind::Void); }
    const Type *intType() const    { return get(Kind::Int); }
    const Type *doubleType() const { return get(Kind::Double); }
    const Type *charType() const   { return get(Kind::Char); }

private:
    std::vector<Type> types_;
    std::vector<Type *> derived_;
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
    // Whether a word may sit at any address: the hosts' loads do, the C6000's LDW faults.
    virtual bool loadsUnaligned() const { return true; }
    // **How a cleanup pad hands the exception back**: `_Unwind_Resume(exception)`, or TI's `__cxa_end_cleanup()`.
    virtual bool resumeTakesException() const { return true; }
    // **Whether `__cxa_get_exception_ptr` exists** - TI's runtime copies from what `__cxa_begin_catch` returns.
    virtual bool hasGetExceptionPtr() const { return true; }

    virtual const char *name() const = 0;
};
