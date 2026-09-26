#include "Mangle.h"

#include "Operator.h"

// "N::M::f" -> {"N", "M", "f"}. A namespace scope is spelled by both ABIs
// exactly as a class scope is - measured, `N::f` is `_ZN1N1fEi` and
// `?f@N@@YAHH@Z` - so the manglers need the components and nothing else.
static std::vector<std::string> scopeComponents(const std::string &name) {
    std::vector<std::string> out;
    std::size_t at = 0;
    for (;;) {
        const std::size_t sep = name.find("::", at);
        if (sep == std::string::npos) { out.push_back(name.substr(at)); break; }
        out.push_back(name.substr(at, sep - at));
        at = sep + 2;
    }
    return out;
}

#include "Type.h"

#include <vector>

// **A conversion function is told by the space in its name.** It is filed as
// `operator bool`; every operator that can be overloaded is punctuation and is
// filed as `operator+`, with none.
static bool isConversionFunction(const std::string &name) {
    return name.compare(0, 9, "operator ") == 0;
}

namespace {

// A type is const-qualified in its own right when it is not the same object as its unqualified self.
bool qualifiedItself(const Type *t) { return t->unqualified() != t; }

const char *itaniumBuiltin(Kind k) {
    switch (k) {
    case Kind::Void:       return "v";
    case Kind::Bool:       return "b";
    case Kind::Char:       return "c";
    case Kind::SChar:      return "a";
    case Kind::UChar:      return "h";
    case Kind::Short:      return "s";
    case Kind::UShort:     return "t";
    case Kind::WChar:      return "w";
    case Kind::Char16:     return "Ds";
    case Kind::Char32:     return "Di";
    case Kind::Int:        return "i";
    case Kind::UInt:       return "j";
    case Kind::Long:       return "l";
    case Kind::ULong:      return "m";
    case Kind::LongLong:   return "x";
    case Kind::ULongLong:  return "y";
    case Kind::Float:      return "f";
    case Kind::Double:     return "d";
    case Kind::LongDouble: return "e";
    // Measured: `void f(decltype(nullptr))` is `_Z1fDn`.
    case Kind::NullPtr:    return "Dn";
    default:               return nullptr;
    }
}

const char *microsoftBuiltin(Kind k) {
    switch (k) {
    case Kind::Void:       return "X";
    case Kind::Bool:       return "_N";
    case Kind::Char:       return "D";
    case Kind::SChar:      return "C";
    case Kind::UChar:      return "E";
    case Kind::Short:      return "F";
    case Kind::UShort:     return "G";
    case Kind::WChar:      return "_W";
    case Kind::Char16:     return "_S";
    case Kind::Char32:     return "_U";
    case Kind::Int:        return "H";
    case Kind::UInt:       return "I";
    case Kind::Long:       return "J";
    case Kind::ULong:      return "K";
    case Kind::LongLong:   return "_J";
    case Kind::ULongLong:  return "_K";
    case Kind::Float:      return "M";
    case Kind::Double:     return "N";
    case Kind::LongDouble: return "O";
    // Measured with clang: `void f(decltype(nullptr))` is `?f@@YAX$$T@Z`.
    case Kind::NullPtr:    return "$$T";
    default:               return nullptr;
    }
}

class Mangler {
public:
    bool ok = true;
    std::string problem;
    std::string out;

protected:
    void refuse(const std::string &why) {
        if (ok) { ok = false; problem = why; }
    }
    const std::string *tagOf(const Type *t) {
        const std::string &tag = t->tag();
        if (tag.empty()) {
            refuse("an unnamed class has no name to give the linker - give the "
                   "struct or union a tag, or reach it through a typedef, "
                   "which names it");
            return nullptr;
        }
        return &tag;
    }
};

// ---------------------------------------------------------------- Itanium

class Itanium : public Mangler {
public:
    // <nested-name> ::= N [<CV>] <prefix> <unqualified-name> E, with the K for a
    // const `this` before the class - _ZNK5Point4cgetEv. Every enclosing class and
    // namespace is a substitution candidate of its own; the caller writes the name.
    void namespacesOf(const std::string &qualified) {
        const std::vector<std::string> parts = scopeComponents(qualified);
        std::vector<std::string> reach;               // N, then N::M, ...
        std::string sofar;
        for (std::size_t i = 0; i + 1 < parts.size(); i++) {
            sofar += sofar.empty() ? parts[i] : "::" + parts[i];
            reach.push_back(sofar);
        }
        // **The longest prefix already in the table stands for the whole of
        // it.** Writing `S_` for N and then `S0_` for N::M spells the same
        // scope twice; N::M alone is what clang writes.
        std::size_t start = 0;
        for (std::size_t i = reach.size(); i-- > 0; ) {
            if (!substitutedName(reach[i])) continue;
            start = i + 1;
            break;
        }
        for (std::size_t i = start; i < reach.size(); i++) {
            // **`std` is written `St`** - [mangle.substitution] gives it one
            // of the predefined abbreviations, so `std::string` is `St6string`
            // and not `N3std6stringE`.
            if (i == 0 && parts[i] == "std") {
                out += "St";
                continue;
            }
            out += std::to_string(parts[i].size());
            out += parts[i];
            Sub s;
            s.name = reach[i];
            subs_.push_back(s);
        }
    }

    // Is this qualified name exactly `std::X`? Then it needs no `N...E` around
    // it: the abbreviation is a prefix in its own right, and clang writes
    // `St6string` where `std::deep::inner` is `NSt4deep5innerE`.
    static bool isDirectlyInStd(const std::string &qualified) {
        const std::vector<std::string> parts = scopeComponents(qualified);
        return parts.size() == 2 && parts[0] == "std";
    }

    // The enclosing function as it goes inside `Z...E`: the mangled name with its
    // `_Z` taken off, or - for main and anything extern "C" - the plain name as a
    // length and letters. Measured: _ZZ4mainEN1B3getEv.
    static std::string functionComponent(const std::string &owner) {
        if (owner.compare(0, 2, "_Z") == 0) return owner.substr(2);
        return std::to_string(owner.size()) + owner;
    }

    void prefix(const Type *cls, const std::string &fallback) {
        if (cls == nullptr) {
            // A namespace-qualified tag with no Type to walk: split it, the
            // same way a free function's name is split.
            namespacesOf(fallback);
            const std::string leaf = scopeComponents(fallback).back();
            out += std::to_string(leaf.size());
            out += leaf;
            return;
        }
        if (substituted(cls)) return;
        if (cls->enclosing() != nullptr) prefix(cls->enclosing(), std::string());
        // A class in a namespace has no enclosing Type; its namespaces are in its
        // tag, and are written and made candidates here. A local class's tag has a
        // "::" too and is not this: `f::L` is one name, so the flag is asked.
        else if (cls->inNamespace()) namespacesOf(cls->tag());
        if (cls->isSpecialization()) {
            templateId(cls);
        } else {
            const std::string &one = cls->localName();
            out += std::to_string(one.size());
            out += one;
        }
        subs_.push_back(Sub{ cls, std::string() });
    }

    // An operator's code stands exactly where an ordinary name writes its length and its letters.
    void writtenName(const std::string &name, bool unary) {
        // **`cv` and then the type**, which is the whole of a conversion
        // function's name on this ABI: `_ZNK1ScvbEv` for `operator bool() const`.
        // The type comes from the *return* type, those being the same thing here.
        if (isConversionFunction(name)) {
            out += "cv";
            type(conversionTo_);
            return;
        }
        const std::string spelling = operatorSpelling(name);
        if (const OperatorCode *op = findOperator(spelling)) {
            out += itaniumOperatorCode(*op, unary);
            return;
        }
        out += std::to_string(name.size());
        out += name;
    }

    // What a conversion function converts to, for writtenName.
    const Type *conversionTo_ = nullptr;

    void memberFunction(const std::string &cls, const Type *clsType,
                        const std::string &name,
                        const Type *fn, bool constThis) {
        conversionTo_ = fn->returns();
        out = "_ZN";
        if (constThis) out += "K";
        prefix(clsType, cls);
        // A member operator's operand count is one more than its parameter
        // count, `this` being the first, so a member with no parameter is the
        // unary form.
        writtenName(name, fn->params().empty());
        out += "E";
        const std::vector<const Type *> &params = fn->params();
        if (params.empty() && !fn->isVariadicFn()) { out += "v"; return; }
        for (const Type *p : params) type(p);
        if (fn->isVariadicFn()) out += "z";
    }

    // A member function *template* specialization: `_ZNK1S3getILi7EEEiv`.
    void memberFunctionTemplate(const std::string &cls, const Type *clsType,
                                const std::string &name, const Type *fn,
                                const std::vector<TemplateArg> &args,
                                bool constThis) {
        conversionTo_ = fn->returns();
        out = "_ZN";
        if (constThis) out += "K";
        prefix(clsType, cls);
        writtenName(name, false);
        out += 'I';
        for (std::size_t i = 0; i < args.size(); i++) templateArgument(args[i]);
        out += 'E';
        out += "E";
        type(fn->returns());
        const std::vector<const Type *> &params = fn->params();
        if (params.empty() && !fn->isVariadicFn()) { out += "v"; return; }
        for (const Type *p : params) type(p);
        if (fn->isVariadicFn()) out += "z";
    }

    // _ZN4PolyaSERKS_ - the class, then `aS` where a member function writes the
    // length and letters of its name, then E and the parameters. There is no return
    // type in an Itanium function name, and the class is candidate zero.
    void copyAssign(const std::string &cls, const Type *clsType, const Type *fn) {
        out = "_ZN";
        prefix(clsType, cls);
        out += "aSE";
        const std::vector<const Type *> &params = fn->params();
        if (params.empty()) { out += "v"; return; }
        for (const Type *p : params) type(p);
    }

    // _ZN5PointC1Eii - the class, then C1 or C2, then E, then the parameters. **A
    // constructor has both names**: C1 builds a complete object and C2 a base
    // subobject, and clang emits both - so C2 is a label in front of C1's body.
    void constructor(const std::string &cls, const Type *clsType,
                     const Type *fn, bool complete) {
        out = "_ZN";
        prefix(clsType, cls);
        out += complete ? "C1E" : "C2E";
        const std::vector<const Type *> &params = fn->params();
        if (params.empty() && !fn->isVariadicFn()) { out += "v"; return; }
        for (const Type *p : params) type(p);
        if (fn->isVariadicFn()) out += "z";
    }

    // D1 complete, D2 base-subobject, D0 the deleting form that lives in a
    // vtable. All three are the same nested-name with two characters after it.
    void destructor(const std::string &cls, const Type *clsType, char form) {
        out = "_ZN";
        prefix(clsType, cls);
        out += 'D';
        out += form;
        out += "Ev";
    }

    void staticMember(const std::string &cls, const Type *clsType,
                      const std::string &name) {
        out = "_ZN";
        prefix(clsType, cls);
        out += std::to_string(name.size());
        out += name;
        out += "E";
    }

    // The type as a signature spells it, with nothing around it - which is what follows `_ZTI`.
    void typeInfoFor(const Type *t) { type(t); }

    void function(const std::string &name, const Type *fn, bool internal) {
        // **The `L` is not written on an operator's name.** clang marks an
        // internal-linkage *identifier* - `_ZL8ordinaryi` - and does not mark
        // an operator, however it is declared.
        if (internal && (findOperator(operatorSpelling(name)) != nullptr ||
                         isConversionFunction(name)))
            internal = false;
        out = internal ? "_ZL" : "_Z";
        // **A name with a scope in it is a nested-name**, `_ZN1N1fEi`, and a
        // namespace component is written exactly as a class one is.
        const std::vector<std::string> parts = scopeComponents(name);
        if (isDirectlyInStd(name)) {
            // `_ZSt15set_new_handlerPFvvE`: the abbreviation alone, no N...E.
            out += "St";
            writtenName(parts.back(), fn->params().size() == 1);
        } else if (parts.size() > 1) {
            out += "N";
            // Every component but the last is a prefix, and a prefix is a
            // substitution candidate - which is what makes a parameter of type
            // N::S inside N::f read `NS_1SE` and not `N1N1SE`.
            namespacesOf(name);
            writtenName(parts.back(), fn->params().size() == 1);
            out += "E";
        } else {
            // A non-member operator carries every operand in its parameter
            // list, so one parameter is the unary form where a member's zero
            // is.
            writtenName(name, fn->params().size() == 1);
        }
        const std::vector<const Type *> &params = fn->params();
        if (params.empty() && !fn->isVariadicFn()) { out += "v"; return; }
        for (const Type *p : params) type(p);
        if (fn->isVariadicFn()) out += "z";
    }

    // A function template specialization, mangled from the template's own signature
    // and its arguments, never the substituted one. **A specialization encodes its
    // return type and an ordinary function does not**, or two would share a symbol.
    void templateFunction(const std::string &name, const Type *pattern,
                          const std::vector<TemplateArg> &args, bool internal) {
        out = internal ? "_ZL" : "_Z";
        // **An operator template spells its code, not the name**, and the code
        // is not a substitution candidate - `operator+<3>` is _ZplILi3EE...,
        // where a named template's name is candidate zero.
        const std::string spelling = operatorSpelling(name);
        if (const OperatorCode *op = findOperator(spelling))
            out += itaniumOperatorCode(*op, pattern->params().size() == 1);
        else {
            out += std::to_string(name.size());
            out += name;
        }
        Sub self;
        self.name = name;
        subs_.push_back(self);

        out += 'I';
        for (std::size_t i = 0; i < args.size(); i++) templateArgument(args[i]);
        out += 'E';

        type(pattern->returns());
        const std::vector<const Type *> &params = pattern->params();
        if (params.empty() && !pattern->isVariadicFn()) { out += "v"; return; }
        for (const Type *p : params) type(p);
        if (pattern->isVariadicFn()) out += "z";
    }

    // `Li3E` - the value, with its own type in front and `n` for a negative one.
    void templateArgument(const TemplateArg &a) {
        // `J i c E` - a pack argument is its members between J and E, and an
        // empty one is `JE`. Measured: _Z7nothingIJicEEiv, _Z5totalIiJEEiT_DpT0_.
        if (a.isPack) {
            out += 'J';
            for (std::size_t i = 0; i < a.pack.size(); i++) type(a.pack[i]);
            out += 'E';
            return;
        }
        // **A non-type parameter as an argument, `V<N>` in a pattern**: written
        // as the expression `X T_ E` - the parameter spelled `T_`, `T0_`, `T1_`
        // for indices 0, 1, 2. Measured: _ZNK1VILi4EE4headILi3EEES_IXT_EEv.
        if (a.isParam) {
            out += "XT";
            if (a.paramIndex > 0) out += std::to_string(a.paramIndex - 1);
            out += "_E";
            return;
        }
        if (a.isType) { type(a.type); return; }
        out += 'L';
        type(a.type);
        if (a.value < 0) { out += 'n'; out += std::to_string(-a.value); }
        else             { out += std::to_string(a.value); }
        out += 'E';
    }

private:
    // **A candidate is a type or a template's name.** Measured: `void two(Holder<int>,
    // Holder<double>)` is _Z3two6HolderIiES_IdE, where the S_ is the word "Holder"
    // and not any type. So the table holds one or the other and never both.
    struct Sub {
        const Type *type = nullptr;
        std::string name;
    };
    std::vector<Sub> subs_;

    // The seq-id: the first repeat is S_, then S0_, S1_ ... S9_, SA_ and on
    // in base 36. Anything past the first is one less than its index, which
    // is the part that is easy to get wrong.
    void seqId(std::size_t i) {
        out += 'S';
        if (i > 0) {
            std::string digits;
            for (std::size_t v = i - 1;; v /= 36) {
                digits.insert(digits.begin(),
                              static_cast<char>(v % 36 < 10 ? '0' + v % 36
                                                            : 'A' + v % 36 - 10));
                if (v < 36) break;
            }
            out += digits;
        }
        out += '_';
    }

    bool substituted(const Type *t) {
        for (std::size_t i = 0; i < subs_.size(); i++)
            if (subs_[i].type == t && subs_[i].name.empty()) { seqId(i); return true; }
        return false;
    }

    bool substitutedName(const std::string &n) {
        for (std::size_t i = 0; i < subs_.size(); i++)
            if (subs_[i].name == n) { seqId(i); return true; }
        return false;
    }

    // `3BoxIiLi3EE` - the template's name, then the arguments between I and E.
    void templateId(const Type *t) {
        // **The namespace the template was declared in**, which the bare name
        // does not carry: `std::vector<int>` is `St6vectorIiE`.
        if (!t->templateNamespace().empty()) {
            const std::string qualified =
                t->templateNamespace() + t->templateName();
            namespacesOf(qualified);
        }
        if (!substitutedName(t->templateName())) {
            out += std::to_string(t->templateName().size());
            out += t->templateName();
            Sub s;
            s.name = t->templateName();
            subs_.push_back(s);
        }
        out += 'I';
        const std::vector<TemplateArg> &args = t->templateArgs();
        for (std::size_t i = 0; i < args.size(); i++) templateArgument(args[i]);
        out += 'E';
    }

    // **An enumeration is spelled exactly as a class is** - measured: `void a(Colour)` is
    // `_Z1a6Colour`, `void b(cc::BinaryOp)` is `_Z1bN2cc8BinaryOpE` with the `N...E` for the same
    // reason, and the whole name is a substitution candidate so `d(Colour, Colour)` is `6ColourS_`.
    void enumeration(const Type *t) {
        const std::vector<std::string> parts = scopeComponents(t->enumTag());
        const bool nested = parts.size() > 1;
        if (nested) out += 'N';
        namespacesOf(t->enumTag());
        out += std::to_string(parts.back().size());
        out += parts.back();
        if (nested) out += 'E';
        subs_.push_back(Sub{ t, std::string() });
    }

    void type(const Type *t) {
        if (!ok) return;
        if (substituted(t)) return;

        // **The qualifier comes first, and asking about it first is what makes
        // `const T &` come out RKT_ rather than RT_.** A qualified copy of a
        // template parameter still answers TemplateParam, so a kind branch drops K.
        if (qualifiedItself(t)) {
            out += 'K';
            type(t->unqualified());
            subs_.push_back(Sub{ t, std::string() });
            return;
        }

        // `Dp` and then what is being expanded, which is a parameter and so
        // says the same thing however many members the pack has. Measured:
        // `void f(Ts...)` is `DpT_` at every size.
        if (t->kind() == Kind::PackExpansion) {
            out += "Dp";
            type(t->pointee());
            subs_.push_back(Sub{ t, std::string() });
            return;
        }

        // `N <owner> <len><name> E` - measured: `Value<T>::type` in a return type is
        // `N5ValueIT_E4typeE` and `T::type` is `NT_4typeE`. A nested-name like any
        // other, and the only new thing is what stands in the prefix.
        if (t->kind() == Kind::DependentMember) {
            out += 'N';
            type(t->pointee());
            out += std::to_string(t->tag().size());
            out += t->tag();
            out += 'E';
            subs_.push_back(Sub{ t, std::string() });
            return;
        }

        // **`T_` is the first parameter and `T0_` the second** - the same
        // seq-id shape the substitution table uses, and a candidate in that
        // table itself, which is what makes the second mention of T an S.
        if (t->kind() == Kind::TemplateParam) {
            out += 'T';
            if (t->length() > 0) out += std::to_string(t->length() - 1);
            out += '_';
            subs_.push_back(Sub{ t, std::string() });
            return;
        }
        // **Before the builtin code**, because an enumeration *is* `Kind::Int`
        // here and would otherwise come out `i`. Asked here rather than in the
        // chain below, which the builtin return never reaches.
        if (t->isEnumeration()) { enumeration(t); return; }
        if (const char *b = itaniumBuiltin(t->kind())) { out += b; return; }

        if (t->isPointer())        { out += 'P'; type(t->pointee()); }
        // **`M` and then the class and the member's type**.
        else if (t->isMemberPointer() || t->isMemberFunctionPointer()) {
            // The same `M` for both: `int S::*` is `M1Si` and `int (S::*)()` is
            // `M1SFivE`. A member function pointer wears the shape of a struct so
            // the backends can copy it, so it is asked about separately.
            out += 'M';
            type(t->unqualified()->enclosing());
            type(t->unqualified()->pointee());
        }
        // `R` for an lvalue reference and `O` for an rvalue one - measured:
        // `f(int &&)` is _Z1fOi where `g(const int &)` is _Z1gRKi.
        else if (t->isReference()) {
            out += t->isRValueReference() ? 'O' : 'R';
            type(t->referent());
        }
        else if (t->isArray()) {
            out += 'A';
            out += std::to_string(t->length());
            out += '_';
            type(t->pointee());
        } else if (t->isFunction()) {
            out += 'F';
            type(t->returns());
            if (t->params().empty() && !t->isVariadicFn()) out += 'v';
            for (const Type *p : t->params()) type(p);
            if (t->isVariadicFn()) out += 'z';
            out += 'E';
        } else if (t->isStructOrUnion()) {
            // A class inside another is a nested-name here too, and prefix()
            // is what consults the substitution table on the way down.
            if (t->enclosing() != nullptr || t->inNamespace()) {
                if (tagOf(t) == nullptr) return;
                // `std::X` is `StX` with no wrapper - see isDirectlyInStd.
                const bool bare = t->enclosing() == nullptr &&
                                  isDirectlyInStd(t->tag());
                if (!bare) out += 'N';
                prefix(t, std::string());
                if (!bare) out += 'E';
                return;                       // prefix() pushed it already
            }
            if (t->isSpecialization()) {
                // **A specialization in a namespace is a nested-name as much as a plain class in
                // one is**, and this branch was writing the bare template-id: `_Z1f2ns1RIiE` where
                // clang writes `_Z1fN2ns1RIiEE`, measured.
                const bool wrapped =
                    !t->templateNamespace().empty() &&
                    !isDirectlyInStd(t->templateNamespace() + t->templateName());
                if (wrapped) out += 'N';
                templateId(t);
                if (wrapped) out += 'E';
                subs_.push_back(Sub{ t, std::string() });
                return;                       // pushed here, not below
            }
            // **A local class is a <local-name>, not a name with a scope in it**:
            // `Z <function> E <name>`, measured - _Z1fIZ4mainE1LEiT_. Spelling the
            // tag whole put a `::` in the symbol, which the assembler refuses.
            if (!t->localOwner().empty()) {
                out += 'Z';
                out += functionComponent(t->localOwner());
                out += 'E';
                const std::string &one = t->localName();
                out += std::to_string(one.size());
                out += one;
                subs_.push_back(Sub{ t, std::string() });
                return;
            }
            const std::string *tag = tagOf(t);
            if (tag == nullptr) return;
            out += std::to_string(tag->size());
            out += *tag;
        } else {
            refuse("this type has no Itanium linkage name yet");
            return;
        }
        subs_.push_back(Sub{ t, std::string() });
    }
};

// -------------------------------------------------------------- Microsoft

class Microsoft : public Mangler {
public:
    // **The class as a type descriptor spells it**, `?AUBase@@`.
    void classAsTypeName(const Type *t) { returnType(t); }
    void anyType(const Type *t) { type(t); }   // a descriptor's pointee, `PEAUB@@`

    void vtableOffset(int offset) { number(offset); }   // a vcall thunk's vftable offset

    // ?name@Class@@ and then four letters - the access, __ptr64, the constness of
    // `this`, the calling convention - with every enclosing class innermost first,
    // closed by '@'. A local class's owner goes in as `?1?` and the whole name.
    void scopeOf(const Type *cls, const std::string &fallback,
                 const std::string &localOwner = std::string()) {
        // Innermost first. A namespace is not a Type, so once the chain of
        // enclosing classes runs out the outermost one's tag is split for
        // whatever namespaces it carries. Measured: `?g@M@N@@YAH...`.
        if (cls == nullptr) {
            const std::vector<std::string> parts = scopeComponents(fallback);
            for (std::size_t i = parts.size(); i-- > 0; ) pushName(parts[i]);
        } else {
            for (const Type *c = cls; c != nullptr; c = c->enclosing()) {
                pushName(componentOf(c));
                if (c->enclosing() == nullptr && c->inNamespace()) {
                    const std::vector<std::string> parts = scopeComponents(c->tag());
                    for (std::size_t i = parts.size() - 1; i-- > 0; )
                        pushName(parts[i]);
                }
                // **A specialization carries its namespace apart from its
                // tag**, which is why the line above cannot find it.
                if (c->enclosing() == nullptr && !c->inNamespace() &&
                    c->isSpecialization() && !c->templateNamespace().empty()) {
                    const std::vector<std::string> parts = scopeComponents(
                        c->templateNamespace() + c->templateName());
                    for (std::size_t i = parts.size() - 1; i-- > 0; )
                        pushName(parts[i]);
                }
            }
        }
        if (!localOwner.empty()) {
            out += "?1?";
            // A function with no decorated name - main, or extern "C" - is
            // written ?name@@9, the 9 saying the name carries no type.
            if (!localOwner.empty() && localOwner[0] == '?') out += localOwner;
            else { out += '?'; out += localOwner; out += "@@9"; }
        }
        out += '@';               // closes the scope list
    }

    // What one scope component is *called*: for an ordinary class its own name, for
    // a specialization the whole template-id, pushed as one name. **Built with the
    // tables put aside**, so an S inside an argument list is invisible outside it.
    std::string componentOf(const Type *c) {
        if (!c->isSpecialization()) return c->localName();

        std::vector<std::string> outerNames;
        std::vector<const Type *> outerArgs;
        std::string outerOut;
        outerNames.swap(names_);
        outerArgs.swap(args_);
        outerOut.swap(out);

        out = "?$";
        pushName(c->templateName());
        const std::vector<TemplateArg> &args = c->templateArgs();
        for (std::size_t i = 0; i < args.size(); i++) templateArgument(args[i]);
        std::string component;
        component.swap(out);

        names_.swap(outerNames);
        args_.swap(outerArgs);
        out.swap(outerOut);
        return component;
    }

    // **An operator replaces the whole `?name@` and is not pushed as a back-reference**.
    bool operatorPrefix(const std::string &name) {
        // **`??B`, and unlike every other operator it writes its return type** - which
        // memberFunction already does for all of them, so naming it is the whole of the difference:
        // `??BS@@QEBA_NXZ` for `operator bool() const`. Measured against clang.
        if (isConversionFunction(name)) { out = "??B"; return true; }
        const OperatorCode *op = findOperator(operatorSpelling(name));
        if (op == nullptr) return false;
        out = "??";
        out += op->microsoft;
        return true;
    }

    void memberFunction(const std::string &cls, const Type *clsType,
                        const std::string &name,
                        const Type *fn, char access, bool constThis,
                        const std::string &localOwner = std::string()) {
        if (!operatorPrefix(name)) {
            out = "?";
            pushName(name);
        }
        scopeOf(clsType, cls, localOwner);
        out += access;            // Q public, I protected, A private
        // **A static member has no `this`, so it spells none.**
        if (access != 'S' && access != 'K' && access != 'C') {
            out += 'E';           // this is __ptr64
            out += constThis ? 'B' : 'A';
        }
        out += 'A';               // __cdecl
        returnType(fn->returns());
        const std::vector<const Type *> &params = fn->params();
        if (params.empty() && !fn->isVariadicFn()) { out += "XZ"; return; }
        for (const Type *p : params) argument(p);
        out += fn->isVariadicFn() ? "ZZ" : "@Z";
    }

    // A member function *template* specialization: `??$get@$06@S@@QEBAHXZ`.
    // `??$` then the member name with its arguments as one template-id, then
    // the class scope, then an ordinary member from there.
    void memberFunctionTemplate(const std::string &cls, const Type *clsType,
                                const std::string &name, const Type *fn,
                                const std::vector<TemplateArg> &args,
                                char access, bool constThis) {
        out = "??$";
        templateId(name, args);
        scopeOf(clsType, cls);
        out += access;
        if (access != 'S' && access != 'K' && access != 'C') {
            out += 'E';
            out += constThis ? 'B' : 'A';
        }
        out += 'A';
        returnType(fn->returns());
        const std::vector<const Type *> &params = fn->params();
        if (params.empty() && !fn->isVariadicFn()) { out += "XZ"; return; }
        for (const Type *p : params) argument(p);
        out += fn->isVariadicFn() ? "ZZ" : "@Z";
    }

    // ??4X@@QEAAAEAU0@AEBU0@@Z - ??4 says operator=, and unlike ??0 it does
    // write the return type. Only the class is pushed as a name, so it is the
    // back-reference 0 rather than the 1 a named member function leaves it.
    void copyAssign(const std::string &cls, const Type *clsType, const Type *fn,
                    char access) {
        out = "??4";
        scopeOf(clsType, cls);
        out += access;
        out += "EAA";
        returnType(fn->returns());
        const std::vector<const Type *> &params = fn->params();
        if (params.empty()) { out += "XZ"; return; }
        for (const Type *p : params) argument(p);
        out += "@Z";
    }

    // ??0Point@@QEAA@HH@Z - ??0 says constructor, and the '@' after QEAA sits
    // where a member function writes its return type.
    void constructor(const std::string &cls, const Type *clsType, const Type *fn,
                     char access) {
        out = "??0";
        // A class written in a function body carries its owner, and the
        // scope has to say so or two functions' `struct L` are one symbol.
        scopeOf(clsType, cls,
                clsType != nullptr ? clsType->localOwner() : std::string());
        out += access;
        out += "EAA";
        out += '@';               // a constructor returns nothing to say
        const std::vector<const Type *> &params = fn->params();
        if (params.empty() && !fn->isVariadicFn()) { out += "XZ"; return; }
        for (const Type *p : params) argument(p);
        out += fn->isVariadicFn() ? "ZZ" : "@Z";
    }

    void destructor(const std::string &cls, const Type *clsType, char access) {
        out = "??1";
        // A class written in a function body carries its owner, and the
        // scope has to say so or two functions' `struct L` are one symbol.
        scopeOf(clsType, cls,
                clsType != nullptr ? clsType->localOwner() : std::string());
        out += access;
        out += "EAA";
        out += "@XZ";
    }

    // ??_GVB@@UEAAPEAXI@Z - the deleting destructor, which takes a flag beside
    // `this` and answers `this`.
    void deletingDestructor(const std::string &cls, const Type *clsType) {
        out = "??_G";
        // A class written in a function body carries its owner, and the
        // scope has to say so or two functions' `struct L` are one symbol.
        scopeOf(clsType, cls,
                clsType != nullptr ? clsType->localOwner() : std::string());
        out += "UEAAPEAXI@Z";
    }

    void function(const std::string &name, const Type *fn) {
        const std::vector<std::string> parts = scopeComponents(name);
        if (parts.size() > 1) {
            // `?g@M@N@@YAHXZ` - the name, then the scopes innermost first, then the
            // '@' that closes the list, a namespace being a scope like any other.
            // An operator written in one keeps its code: `??HN@@YA...`.
            if (!operatorPrefix(parts.back())) {
                out = "?";
                pushName(parts.back());
            }
            for (std::size_t i = parts.size() - 1; i-- > 0; ) pushName(parts[i]);
            out += '@';
        } else if (operatorPrefix(name)) {
            out += '@';           // the empty list of scopes
        } else {
            out = "?";
            nameComponent(name);  // the name, and the empty list of scopes
        }
        out += "YA";              // a free function, __cdecl
        returnType(fn->returns());
        const std::vector<const Type *> &params = fn->params();
        if (params.empty() && !fn->isVariadicFn()) { out += "XZ"; return; }
        for (const Type *p : params) argument(p);
        out += fn->isVariadicFn() ? "ZZ" : "@Z";
    }

    // ??$twice@H@@YAHH@Z - `??$` where an ordinary function has `?`, the template-id
    // as one scope component, the empty enclosing scope list, and from there an
    // ordinary free function. The signature written is the *substituted* one.
    void templateFunction(const std::string &name, const Type *fn,
                          const std::vector<TemplateArg> &args) {
        out = "??$";
        templateId(name, args);
        out += '@';               // the empty enclosing scope list
        out += "YA";
        returnType(fn->returns());
        const std::vector<const Type *> &params = fn->params();
        if (params.empty() && !fn->isVariadicFn()) { out += "XZ"; return; }
        for (const Type *p : params) argument(p);
        out += fn->isVariadicFn() ? "ZZ" : "@Z";
    }

    // **A template-id carries back-reference tables of its own.** Measured with cl:
    // `T same(T)` at T=S is ??$same@US@@@@YA?AUS@@U0@, where the parameter's 0 is
    // the S the *return type* pushed. So both tables are put aside and put back.
    void templateId(const std::string &name,
                    const std::vector<TemplateArg> &args) {
        std::vector<std::string> outerNames;
        std::vector<const Type *> outerArgs;
        outerNames.swap(names_);
        outerArgs.swap(args_);
        // **An operator template-id spells the operator code, not the name**, and
        // takes no name back-reference: `operator+<3>` is ??$?H$02@..., where ?H
        // is the code that ??H writes with the leading ?? already spent on ??$.
        const OperatorCode *op = findOperator(operatorSpelling(name));
        if (isConversionFunction(name)) {
            out += "?B";
        } else if (op != nullptr) {
            out += '?';
            out += op->microsoft;
        } else {
            pushName(name);
        }
        for (std::size_t i = 0; i < args.size(); i++) templateArgument(args[i]);
        out += '@';               // closes the template-id
        names_.swap(outerNames);
        args_.swap(outerArgs);
    }

    // `$02` for 3, and `$0?4` for -5.
    void templateArgument(const TemplateArg &a) {
        // A pack's members are written one after another with nothing around
        // them, and an empty pack is `$$V`. Measured with cl:
        // ??$total@HH@@YAHHH@Z for a pack of one, ??$total@H$$V@@YAHH@Z for none.
        if (a.isPack) {
            if (a.pack.empty()) { out += "$$V"; return; }
            for (std::size_t i = 0; i < a.pack.size(); i++) {
                TemplateArg one;
                one.type = a.pack[i];
                templateArgument(one);
            }
            return;
        }
        if (a.isType) {
            // **A top-level const on a type argument is spelled `$$CB`**, and only
            // where the thing under it is not a pointer. Without it the const was
            // dropped and `W<const int>` shared one symbol with `W<int>`.
            if (a.type->unqualified() != a.type && !a.type->isPointer()) {
                out += "$$CB";
                type(a.type->unqualified());
                return;
            }
            type(a.type);
            return;
        }
        out += "$0";
        if (a.value < 0) { out += '?'; number(-a.value); }
        else             { number(a.value); }
    }

    // ?pub@C@@2HA - the name, the class, then the access as a digit and the
    // type spelled the way a data symbol spells it. Measured with cl.
    void staticMember(const std::string &cls, const Type *clsType,
                      const std::string &name, const Type *t, char access) {
        out = "?";
        pushName(name);
        scopeOf(clsType, cls);
        out += access;            // '2' public, '1' protected, '0' private
        dataType(t);
    }

    void data(const std::string &name, const Type *t) {
        out = "?";
        // `?v@N@@3HA` - the name, then the namespaces innermost first, then
        // the '@' that closes the list, exactly as a function's scopes go.
        const std::vector<std::string> parts = scopeComponents(name);
        if (parts.size() > 1) {
            pushName(parts.back());
            for (std::size_t i = parts.size() - 1; i-- > 0; ) pushName(parts[i]);
            out += '@';
        } else {
            nameComponent(name);
        }
        out += "3";               // a variable at namespace scope
        dataType(t);
    }

    // **A data symbol's type is not spelled the way a parameter's is**, and the
    // three differences were measured with cl: an array decays (`?arr@@3PAHA`), the
    // qualifier carries E, and it repeats the POINTEE's const (`?ccp@@3PEBDEB`).
    void dataType(const Type *t) {
        const Type *u = t->unqualified();
        if (u->isArray()) {
            out += "PA";
            type(u->pointee());
            out += t->isConst() ? "B" : "A";
            return;
        }
        type(u);
        // **A member pointer's storage class repeats the class**, `EQ<class>@`
        // after the type: cl's `?gpm@@3PEQNest@@HEQ1@` (the review's A7); Q is
        // the member's cv-code, R for const, as a pointer's A/B are.
        if (u->isMemberPointer() || u->isMemberFunctionPointer()) {
            const Type *cls = u->enclosing();
            out += 'E';
            out += t->isConst() ? 'R' : 'Q';
            scopeOf(cls, cls->tag());
            return;
        }
        // A reference follows the pointer's rule, measured: `?r@@3AEAHEA`,
        // `?cr@@3AEBHEB`, `?sr@@3AEAUS@@EA`.
        if (u->isPointer() || u->isReference()) {
            const Type *at = u->isPointer() ? u->pointee() : u->referent();
            out += 'E';
            out += at->isConst() ? 'B' : 'A';
            return;
        }
        out += t->isConst() ? "B" : "A";
    }

    // The scope list a vftable's name carries.
    void vftableScope(const Type *cls) { scopeOf(cls, cls->tag()); }

private:
    std::vector<std::string> names_;
    std::vector<const Type *> args_;

    // Names repeat by index, the function's own name being the first in the table,
    // and **a back-reference digit replaces the whole component, its '@' included**
    // - clang writes AEBV1@@Z where digit-plus-'@' would give V1@@. Measured.
    void pushName(const std::string &n) {
        for (std::size_t i = 0; i < names_.size(); i++)
            if (names_[i] == n) { out += static_cast<char>('0' + i); return; }
        if (names_.size() < 10) names_.push_back(n);
        out += n;
        out += '@';
    }

    void nameComponent(const std::string &n) {
        pushName(n);
        out += '@';               // and the empty scope list
    }

    // A name whose scopes are written into it - `N::S`, which has no `enclosing()`
    // to walk, a namespace not being a Type. Innermost first, as scopeOf() walks,
    // each component a candidate: `use(N::S)` is `?use@@YAHUS@N@@@Z`.
    void qualifiedName(const std::string &n) {
        const std::vector<std::string> parts = scopeComponents(n);
        for (std::size_t i = parts.size(); i-- > 0; ) pushName(parts[i]);
        out += '@';               // closes the scope list
    }

    // Numbers: one less than the value as a digit up to ten, and the value
    // itself in hexadecimal above that, with A standing for 0 and a '@' to
    // close it. Both halves of that were measured against clang.
    void number(long long v) {
        if (v == 0)               { out += "A@"; return; }
        if (v >= 1 && v <= 10)    { out += static_cast<char>('0' + v - 1); return; }
        std::string digits;
        for (long long x = v; x != 0; x /= 16)
            digits.insert(digits.begin(), static_cast<char>('A' + x % 16));
        out += digits;
        out += '@';
    }

    void returnType(const Type *t) {
        // A class or an enumeration returned by value carries its
        // cv-qualification in front of it; everything else is written as it
        // stands. Measured: `Small pick(int)` is `?pick@@YA?AW4Small@@H@Z`.
        if (t->isStructOrUnion() || t->isEnumeration()) out += "?A";
        type(t);
    }

    // An argument that is not a plain builtin can be named again by index.
    void argument(const Type *t) {
        // **An enumeration takes a slot though its kind is `Kind::Int`** - it
        // is spelled `W4Colour@@` rather than `H`, and a repeat is the digit:
        // `d(Colour, Colour)` is `W4Colour@@0@Z`, measured.
        if (microsoftBuiltin(t->kind()) == nullptr || t->isEnumeration()) {
            for (std::size_t i = 0; i < args_.size(); i++)
                if (args_[i] == t) { out += static_cast<char>('0' + i); return; }
            // **A type takes its slot after it is spelled, not before.**
            type(t);
            if (args_.size() < 10) args_.push_back(t);
            return;
        }
        type(t);
    }

    void pointee(const Type *p) {
        // A function has no storage class after the pointer that reaches it,
        // and an array carries its dimensions there instead.
        if (p->isFunction()) {
            out += "6A";
            type(p->returns());
            if (p->params().empty() && !p->isVariadicFn()) { out += "XZ"; return; }
            // **The argument table is one table for the whole name**, and a
            // parameter inside a function pointer's signature goes into it
            // like any other.
            for (const Type *a : p->params()) argument(a);
            out += p->isVariadicFn() ? "ZZ" : "@Z";
            return;
        }
        // The qualifier asked about here is the pointee's own. An array of const
        // elements is not itself const - the elements carry that, and are marked
        // where they are written - so isConst() is the wrong question.
        out += qualifiedItself(p) ? "EB" : "EA";
        type(p);
    }

    void type(const Type *t) {
        if (!ok) return;

        if (t->isPointer())        { out += qualifiedItself(t) ? 'Q' : 'P';
                                     pointee(t->pointee()); return; }
        // **`PEQ` and then the class's scope and the member's type** - measured,
        // `int S::*` is `PEQS@@H` where an ordinary `int *` is `PEAH`. The Q sits
        // where a pointer's A does, the class between it and the type.
        if (t->isMemberPointer()) {
            const Type *plain = t->unqualified();
            out += "PEQ";
            scopeOf(plain->enclosing(), plain->enclosing()->tag());
            type(plain->pointee());
            return;
        }
        // **`P8` and then the class, where a data member pointer writes `PEQ`** -
        // measured, `int (S::*)()` is `P8S@@EAAHXZ`. The `EAA` is the same
        // near/__cdecl/non-const `this` a member function's own name carries.
        if (t->isMemberFunctionPointer()) {
            const Type *plain = t->unqualified();
            const Type *fn = plain->pointee();
            out += "P8";
            scopeOf(plain->enclosing(), plain->enclosing()->tag());
            out += "EAA";
            returnType(fn->returns());
            const std::vector<const Type *> &ps = fn->params();
            if (ps.empty() && !fn->isVariadicFn()) { out += "XZ"; return; }
            for (const Type *one : ps) argument(one);
            out += fn->isVariadicFn() ? "ZZ" : "@Z";
            return;
        }
        // `$$Q` where an lvalue reference writes `A`, and the qualifier that
        // follows is the same one either way - measured with clang for the
        // Microsoft target: `?f@@YAH$$QEAH@Z` beside `?g@@YAHAEBH@Z`.
        if (t->isReference()) {
            if (t->isRValueReference()) out += "$$Q";
            else                        out += 'A';
            pointee(t->referent());
            return;
        }
        if (t->isArray()) {
            out += 'Y';
            std::vector<long long> dims;
            const Type *elem = t;
            while (elem->isArray()) { dims.push_back(elem->length()); elem = elem->pointee(); }
            number(static_cast<long long>(dims.size()));
            for (long long d : dims) number(d);
            // A const element is written with the qualifier marker rather
            // than by qualifying the array, which has no qualifier of its own.
            if (elem->isConst()) out += "$$CB";
            type(elem->unqualified());
            return;
        }
        // **`W4` and then the name, exactly where a class writes its letter**
        // - measured: `void a(Colour)` is `?a@@YAXW4Colour@@@Z` and `void
        // b(cc::BinaryOp)` is `?b@@YAXW4BinaryOp@cc@@@Z`.
        if (t->isEnumeration()) {
            out += "W4";
            scopeOf(nullptr, t->enumTag());
            return;
        }
        if (const char *b = microsoftBuiltin(t->kind())) { out += b; return; }
        if (t->isStructOrUnion()) {
            const std::string *tag = tagOf(t);
            if (tag == nullptr) return;
            // **T union, U struct, V class** - the Microsoft ABI spells the three
            // differently, and until vtables nothing declared with `class` reached
            // a name, so U covered both. Itanium spells all three by tag.
            out += t->kind() == Kind::Union ? 'T'
                 : t->declaredClass()       ? 'V'
                                            : 'U';
            // A class written inside a function carries that function round it here
            // too, and `scopeOf` already knows the shape. Measured against clang:
            // `??$f@UL@?1??main@@9@@@YAHUL@?1??main@@9@@Z`.
            if (!t->localOwner().empty()) {
                scopeOf(t, std::string(), t->localOwner());
                return;
            }
            if (t->enclosing() != nullptr || t->isSpecialization() ||
                t->inNamespace()) {
                scopeOf(t, std::string());
                return;
            }
            qualifiedName(*tag);
            return;
        }
        refuse("this type has no Microsoft linkage name yet");
    }
};

}  // namespace

bool itaniumFunctionName(const std::string &name, const Type *fn, bool internal,
                         std::string *out, std::string *problem) {
    Itanium m;
    m.function(name, fn, internal);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool microsoftFunctionName(const std::string &name, const Type *fn, bool internal,
                           std::string *out, std::string *problem) {
    (void)internal;   // the Microsoft ABI spells an internal function the same
    Microsoft m;
    m.function(name, fn);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

const char *itaniumBuiltinCode(Kind k) { return itaniumBuiltin(k); }

std::string vtableSymbol(const std::string &tag, bool microsoft) {
    const std::vector<std::string> parts = scopeComponents(tag);
    if (microsoft) {
        std::string out = "??_7";
        for (std::size_t i = parts.size(); i-- > 0; ) { out += parts[i]; out += '@'; }
        return out + "@6B@";
    }
    return "_ZTV" + itaniumClassNameString(tag);   // the St abbreviation included
}

// **A class's type_info and the string beside it, which are one encoding under
// two prefixes.**
std::string vbtableSymbol(const std::string &tag) {
    const std::vector<std::string> parts = scopeComponents(tag);
    std::string out = "??_8";
    for (std::size_t i = parts.size(); i-- > 0; ) { out += parts[i]; out += '@'; }
    return out + "@7B@";
}

// **The one a class with more than one vbptr gets**, named for the base whose
// subobject holds that pointer: `??_8Dia@@7BD1@@@`. cl names it after the
// *direct* base and not after the class that introduced the pointer.
std::string vbtableSymbol(const std::string &tag, const std::string &base) {
    const std::vector<std::string> parts = scopeComponents(tag);
    std::string out = "??_8";
    for (std::size_t i = parts.size(); i-- > 0; ) { out += parts[i]; out += '@'; }
    out += "@7B";
    const std::vector<std::string> bp = scopeComponents(base);
    for (std::size_t i = bp.size(); i-- > 0; ) { out += bp[i]; out += '@'; }
    return out + "@@";
}

// **The vbase destructor, which only the Microsoft ABI has.**
std::string vbaseDestructorSymbol(const std::string &tag) {
    const std::vector<std::string> parts = scopeComponents(tag);
    std::string out = "??_D";
    for (std::size_t i = parts.size(); i-- > 0; ) { out += parts[i]; out += '@'; }
    return out + "@QEAAXXZ";
}

std::string itaniumClassNameString(const std::string &tag) {
    const std::vector<std::string> parts = scopeComponents(tag);
    std::string out;
    // `std::X` is `St1X` and `std::a::X` is `NSt1a1XE` - the abbreviation
    // the type encoding already writes, so `_ZTISt9bad_alloc` is the runtime's.
    const bool inStd = parts.size() > 1 && parts[0] == "std";
    const bool nested = parts.size() > 2 || (parts.size() > 1 && !inStd);
    if (nested) out += 'N';
    for (std::size_t i = 0; i < parts.size(); i++) {
        if (i == 0 && inStd) { out += "St"; continue; }
        out += std::to_string(parts[i].size());
        out += parts[i];
    }
    if (nested) out += 'E';
    return out;
}

std::string itaniumClassTypeInfoSymbol(const std::string &tag) {
    return "_ZTI" + itaniumClassNameString(tag);
}

std::string itaniumClassTypeNameSymbol(const std::string &tag) {
    return "_ZTS" + itaniumClassNameString(tag);
}

// **A class whose name is a template-id is spelled by the mangler, not by
// counting letters.**
static bool tagIsTemplateId(const std::string &tag) {
    return tag.find('<') != std::string::npos;
}

std::string vtableSymbol(const Type *cls, bool microsoft) {
    if (cls == nullptr) return vtableSymbol(std::string(), microsoft);
    const Type *plain = cls->unqualified();
    if (!tagIsTemplateId(plain->tag())) return vtableSymbol(plain->tag(), microsoft);
    if (microsoft) {
        Microsoft m;
        m.out = "??_7";
        m.vftableScope(plain);
        if (!m.ok) return vtableSymbol(plain->tag(), microsoft);
        return m.out + "6B@";
    }
    Itanium m;
    m.out = "_ZTV";
    m.typeInfoFor(plain);
    if (!m.ok) return vtableSymbol(plain->tag(), microsoft);
    return m.out;
}

std::string itaniumClassNameString(const Type *cls) {
    if (cls == nullptr) return itaniumClassNameString(std::string());
    const Type *plain = cls->unqualified();
    if (!tagIsTemplateId(plain->tag())) return itaniumClassNameString(plain->tag());
    Itanium m;
    m.typeInfoFor(plain);
    if (!m.ok) return itaniumClassNameString(plain->tag());
    return m.out;
}

std::string itaniumClassTypeInfoSymbol(const Type *cls) {
    return "_ZTI" + itaniumClassNameString(cls);
}

std::string itaniumClassTypeNameSymbol(const Type *cls) {
    return "_ZTS" + itaniumClassNameString(cls);
}

bool itaniumTypeSpelling(const Type *t, std::string *out, std::string *problem) {
    Itanium m;
    m.typeInfoFor(t);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool itaniumTypeInfoName(const Type *t, std::string *out, std::string *problem) {
    // An enumeration is its integer here, but its type_info is its own - an
    // __enum_type_info the compiler emits, not the library's fundamental one.
    if (itaniumBuiltin(t->kind()) == nullptr || t->isEnumeration()) {
        *problem = "only a fundamental type has a type_info object the "
                   "standard library already carries; one for '" +
                   t->describe() + "' would have to be emitted here, and that "
                   "is its own step";
        return false;
    }
    Itanium m;
    m.out = "_ZTI";
    m.typeInfoFor(t);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool microsoftVcallThunkName(const Type *cls, int offset, std::string *out,
                             std::string *problem) {
    Microsoft scope;
    scope.scopeOf(cls, cls->tag());
    if (!scope.ok) { *problem = scope.problem; return false; }
    Microsoft n;
    n.vtableOffset(offset);
    *out = "??_9" + scope.out + "$B" + n.out + "AA";
    return true;
}

// **The five objects the Microsoft ABI wants before it will answer a
// `dynamic_cast`**, measured from clang.
bool microsoftClassRttiNames(const Type *cls, MicrosoftRtti *out,
                             std::string *problem) {
    if (!cls->isStructOrUnion()) {
        *problem = "'" + cls->describe() + "' is not a class, so it has no "
                   "run-time class description";
        return false;
    }
    Microsoft type;
    type.classAsTypeName(cls);                  // "?AUBase@@" - the type as a name
    if (!type.ok) { *problem = type.problem; return false; }

    Microsoft scope;
    scope.scopeOf(cls, cls->tag());             // "Base@@" - the class as a scope
    if (!scope.ok) { *problem = scope.problem; return false; }
    // `scopeOf` closes the scope itself, so this is already `Leaf@@`.
    const std::string within = scope.out;

    out->decorated = "." + type.out;
    out->descriptor = "??_R0" + type.out + "@8";
    out->baseDescriptor = "??_R1A@?0A@EA@" + within + "8";
    out->array = "??_R2" + within + "8";
    out->hierarchy = "??_R3" + within + "8";
    out->locator = "??_R4" + within + "6B@";
    return true;
}

// The code a type descriptor spells a type by: a builtin's letter, `?AUE@@` for
// a class, `PEAUE@@` for a pointer - the Microsoft mangler's own spelling.
static bool microsoftDescriptorCode(const Type *t, std::string *code,
                                    std::string *problem) {
    if (const char *letter = microsoftBuiltin(t->kind())) { *code = letter; return true; }
    Microsoft m;
    if (t->isStructOrUnion()) m.classAsTypeName(t);
    else m.anyType(t);
    if (!m.ok) { *problem = m.problem; return false; }
    *code = m.out;
    return true;
}

// Every non-virtual base at any depth, with its offset in the object - the
// types the runtime may catch this one as. A virtual base wants pdisp and
// vdisp, which nothing here writes.
static bool microsoftCatchableBases(const Type *cls, int at, bool pointer,
                                    std::vector<MicrosoftThrow::Catchable> *out,
                                    std::string *problem) {
    const std::vector<Type::BaseSpec> &bs = cls->bases();
    for (std::size_t i = 0; i < bs.size(); i++) {
        if (!bs[i].direct) continue;
        if (bs[i].isVirtual) {
            *problem = "'" + cls->describe() + "' has a virtual base, and the "
                       "catchable-type record for one needs the vbtable "
                       "displacements, which are not written yet";
            return false;
        }
        const Type *b = bs[i].type->unqualified();
        std::string code;
        if (!microsoftDescriptorCode(b, &code, problem)) return false;
        MicrosoftThrow::Catchable c;
        c.cls = pointer ? nullptr : b;
        c.decorated = "." + (pointer ? "PEA" + code.substr(2) : code);
        c.descriptor = "??_R0" + c.decorated.substr(1) + "@8";
        c.mdisp = at + bs[i].offset;
        c.simple = pointer;
        out->push_back(c);
        if (!microsoftCatchableBases(b, at + bs[i].offset, pointer, out, problem))
            return false;
    }
    return true;
}

bool microsoftThrowNames(const Type *t, int size, MicrosoftThrow *out,
                         std::string *problem) {
    out->catchables.clear();
    out->size = size;
    out->isConst = false;
    if (t->isReference()) t = t->referent()->unqualified();
    if (t->isPointer()) {
        // **A pointer to const is spelled without it**, and the ThrowInfo says
        // `C` instead: `_TIC2PEAD` for `const char *`. Measured from clang.
        const Type *pointee = t->pointee();
        out->isConst = pointee->isConst();
        pointee = pointee->unqualified();
        if (pointee->isPointer() || pointee->isFunction() ||
            pointee->isMemberPointer() || pointee->isMemberFunctionPointer()) {
            *problem = "a pointer to a function has no type_info here yet, nor has a "
                       "pointer to a pointer or a member - a thrown pointer to '" +
                       pointee->describe() + "' is not supported yet";
            return false;
        }
        std::string code;
        if (pointee->isVoid()) code = "X";
        else if (!microsoftDescriptorCode(pointee, &code, problem)) return false;
        if (pointee->isStructOrUnion()) code = code.substr(2);   // `?AUB@@` to `UB@@`
        out->decorated = ".PEA" + code;
        out->descriptor = "??_R0PEA" + code + "@8";
        MicrosoftThrow::Catchable self;
        self.decorated = out->decorated;
        self.descriptor = out->descriptor;
        self.size = size;
        self.simple = true;
        out->catchables.push_back(self);
        if (pointee->isStructOrUnion() &&
            !microsoftCatchableBases(pointee, 0, true, &out->catchables, problem))
            return false;
        // And `void *`, which every object pointer may be caught as.
        if (!pointee->isVoid()) {
            MicrosoftThrow::Catchable any;
            any.decorated = ".PEAX";
            any.descriptor = "??_R0PEAX@8";
            any.size = size;
            any.simple = true;
            out->catchables.push_back(any);
        }
        for (std::size_t i = 0; i < out->catchables.size(); i++)
            out->catchables[i].size = size;
        return true;
    }
    std::string code;
    if (!microsoftDescriptorCode(t, &code, problem)) return false;
    out->decorated = "." + code;
    out->descriptor = "??_R0" + code + "@8";
    MicrosoftThrow::Catchable self;
    self.cls = t->isStructOrUnion() ? t : nullptr;
    self.decorated = out->decorated;
    self.descriptor = out->descriptor;
    self.size = size;
    self.simple = !t->isStructOrUnion();
    out->catchables.push_back(self);
    if (t->isStructOrUnion() &&
        !microsoftCatchableBases(t, 0, false, &out->catchables, problem))
        return false;
    return true;
}

bool microsoftTypeidNames(const Type *t, MicrosoftThrow *out, std::string *problem) {
    out->catchables.clear();
    out->thrown = false;
    std::string code;
    if (t->isStructOrUnion() || microsoftBuiltin(t->kind()) != nullptr) {
        if (!microsoftDescriptorCode(t, &code, problem)) return false;
    } else {
        Microsoft m;
        m.anyType(t);
        if (!m.ok) { *problem = m.problem; return false; }
        code = m.out;
    }
    out->decorated = "." + code;
    out->descriptor = "??_R0" + code + "@8";
    MicrosoftThrow::Catchable self;
    self.decorated = out->decorated;
    self.descriptor = out->descriptor;
    out->catchables.push_back(self);
    return true;
}

// Measured with cl: `_CT??_R0H@84`, `_CTA1H`, `_TI1H` for an int; the copy
// constructor's name inside a class's catchable, and its size after it.
void microsoftThrowFinish(MicrosoftThrow *out) {
    for (std::size_t i = 0; i < out->catchables.size(); i++) {
        MicrosoftThrow::Catchable &c = out->catchables[i];
        c.name = "_CT" + c.descriptor + c.copyCtor + std::to_string(c.size);
    }
    const std::string count = std::to_string(out->catchables.size());
    const std::string code = out->decorated.substr(1);
    out->array = "_CTA" + count + code;
    out->info = std::string("_TI") + (out->isConst ? "C" : "") + count + code;
}

bool itaniumTemplateFunctionName(const std::string &name, const Type *pattern,
                                 const std::vector<TemplateArg> &args,
                                 bool internal,
                                 std::string *out, std::string *problem) {
    Itanium m;
    m.templateFunction(name, pattern, args, internal);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool microsoftTemplateFunctionName(const std::string &name, const Type *fn,
                                   const std::vector<TemplateArg> &args,
                                   std::string *out, std::string *problem) {
    Microsoft m;
    m.templateFunction(name, fn, args);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool itaniumMemberName(const std::string &cls, const Type *clsType,
                       const std::string &name,
                       const Type *fn, bool constThis,
                       std::string *out, std::string *problem) {
    Itanium m;
    m.memberFunction(cls, clsType, name, fn, constThis);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

// Itanium wraps the ordinary name rather than building a different one:
// <local-name> ::= Z <function encoding> E <entity>. The member is spelled as it
// would be outside a function, both giving up their _Z to sit in the wrapper.
bool itaniumLocalMemberName(const std::string &owner, const std::string &cls,
                            const Type *clsType, const std::string &name,
                            const Type *fn, bool constThis,
                            std::string *out, std::string *problem) {
    std::string inner;
    if (!itaniumMemberName(cls, clsType, name, fn, constThis, &inner, problem))
        return false;

    std::string function;
    if (inner.compare(0, 2, "_Z") != 0) { *problem = "an unmangled member"; return false; }
    if (owner.compare(0, 2, "_Z") == 0) {
        function = owner.substr(2);
    } else {
        // No mangled name to take apart - `main`, or `extern "C"`. Itanium
        // writes the plain source name as a length-and-letters component,
        // measured: _ZZ4mainEN1B3getEv.
        function = std::to_string(owner.size()) + owner;
    }
    *out = "_ZZ" + function + "E" + inner.substr(2);
    return true;
}

bool microsoftLocalMemberName(const std::string &owner, const std::string &cls,
                              const Type *clsType, const std::string &name,
                              const Type *fn, char access, bool constThis,
                              std::string *out, std::string *problem) {
    Microsoft m;
    m.memberFunction(cls, clsType, name, fn, access, constThis, owner);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool microsoftMemberName(const std::string &cls, const Type *clsType,
                         const std::string &name,
                         const Type *fn, char access, bool constThis,
                         std::string *out, std::string *problem) {
    Microsoft m;
    m.memberFunction(cls, clsType, name, fn, access, constThis);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool itaniumMemberTemplateName(const std::string &cls, const Type *clsType,
                               const std::string &name, const Type *fn,
                               const std::vector<TemplateArg> &args,
                               bool constThis,
                               std::string *out, std::string *problem) {
    Itanium m;
    m.memberFunctionTemplate(cls, clsType, name, fn, args, constThis);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool microsoftMemberTemplateName(const std::string &cls, const Type *clsType,
                                 const std::string &name, const Type *fn,
                                 const std::vector<TemplateArg> &args,
                                 char access, bool constThis,
                                 std::string *out, std::string *problem) {
    Microsoft m;
    m.memberFunctionTemplate(cls, clsType, name, fn, args, access, constThis);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool itaniumCopyAssignName(const std::string &cls, const Type *clsType,
                           const Type *fn, std::string *out, std::string *problem) {
    Itanium m;
    m.copyAssign(cls, clsType, fn);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool microsoftCopyAssignName(const std::string &cls, const Type *clsType,
                             const Type *fn, char access,
                             std::string *out, std::string *problem) {
    Microsoft m;
    m.copyAssign(cls, clsType, fn, access);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

// **A class defined in a function body wraps its whole ordinary name in the
// enclosing function's**.
static void itaniumWrapLocal(const Type *clsType, std::string *out) {
    if (clsType == nullptr) return;
    const std::string &owner = clsType->localOwner();
    if (owner.empty()) return;
    if (out->compare(0, 2, "_Z") != 0) return;
    // No mangled name to take apart - `main`, or `extern "C"` - is written as
    // a plain length-and-letters component, as itaniumLocalMemberName does.
    const std::string function = owner.compare(0, 2, "_Z") == 0
                               ? owner.substr(2)
                               : std::to_string(owner.size()) + owner;
    *out = "_ZZ" + function + "E" + out->substr(2);
}

bool itaniumDestructorName(const std::string &cls, const Type *clsType,
                           bool complete, std::string *out) {
    // A destructor takes nothing and returns nothing, so there is nothing to
    // spell but the class - which is a whole nested-name once a class can be
    // written inside another.
    Itanium m;
    m.destructor(cls, clsType, complete ? '1' : '2');
    *out = m.out;
    itaniumWrapLocal(clsType, out);
    return true;
}

std::string itaniumDeletingDestructorName(const std::string &cls,
                                          const Type *clsType) {
    Itanium m;
    m.destructor(cls, clsType, '0');
    std::string out = m.out;
    itaniumWrapLocal(clsType, &out);
    return out;
}

std::string microsoftDeletingDestructorName(const std::string &cls,
                                            const Type *clsType) {
    Microsoft m;
    m.deletingDestructor(cls, clsType);
    return m.out;
}

std::string microsoftDestructorName(const std::string &cls, const Type *clsType,
                                    char access) {
    Microsoft m;
    m.destructor(cls, clsType, access);
    return m.out;
}

bool itaniumConstructorName(const std::string &cls, const Type *clsType,
                            const Type *fn,
                            bool complete, std::string *out, std::string *problem) {
    Itanium m;
    m.constructor(cls, clsType, fn, complete);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    itaniumWrapLocal(clsType, out);
    return true;
}

bool microsoftConstructorName(const std::string &cls, const Type *clsType,
                              const Type *fn, char access,
                              std::string *out, std::string *problem) {
    Microsoft m;
    m.constructor(cls, clsType, fn, access);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

// **A variable in a namespace is a nested-name**, `_ZN1N1vE` - measured.
// One at file scope keeps the name it was written with, which is what lets
// C name it, and that is the case this used to be the whole of.
std::string itaniumDataName(const std::string &name, bool internal) {
    const std::vector<std::string> parts = scopeComponents(name);
    // `_ZSt7nothrow` and a static `_ZStL4cout`, the abbreviation alone; deeper, `_ZNSt1a1vE`. Measured.
    if (parts.size() == 2 && parts[0] == "std")
        return "_ZSt" + std::string(internal ? "L" : "") +
               std::to_string(parts[1].size()) + parts[1];
    if (parts.size() > 1) {
        std::string out = "_ZN";
        for (std::size_t i = 0; i + 1 < parts.size(); i++) {
            if (i == 0 && parts[i] == "std") { out += "St"; continue; }
            out += std::to_string(parts[i].size());
            out += parts[i];
        }
        // **The L for internal linkage goes before the unqualified name**, not
        // after the `_ZN` where a file-scope static puts it - `_ZN1nL3objE`,
        // measured. Getting it wrong gives a name nothing else spells.
        if (internal) out += "L";
        out += std::to_string(parts.back().size());
        out += parts.back();
        return out + "E";
    }
    if (!internal) return name;
    return "_ZL" + std::to_string(name.size()) + name;
}

std::string itaniumStaticMemberName(const std::string &cls, const Type *clsType,
                                    const std::string &name) {
    Itanium m;
    m.staticMember(cls, clsType, name);
    return m.out;
}

bool microsoftStaticMemberName(const std::string &cls, const Type *clsType,
                               const std::string &name,
                               const Type *t, char access,
                               std::string *out, std::string *problem) {
    Microsoft m;
    m.staticMember(cls, clsType, name, t, access);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}

bool microsoftDataName(const std::string &name, const Type *t,
                       std::string *out, std::string *problem) {
    Microsoft m;
    m.data(name, t);
    if (!m.ok) { *problem = m.problem; return false; }
    *out = m.out;
    return true;
}
