#pragma once

#include "../Abi.h"
#include "../Ast.h"
#include "../Lexer.h"
#include "../Mangle.h"
#include "../Type.h"

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class Source;

class Parser {
public:
    // **The whole ABI table, not the three fields of it a return happens to
    // need.** They arrived as three positional arguments, which is a thing to get
    // in the wrong order once; the parser and the backends now read one struct.
    Parser(const Source &src, std::vector<Token> tokens,
           TypeTable &types, const Target &target, const Abi &abi)
        : src_(src), tokens_(std::move(tokens)), types_(types), target_(target),
          abi_(abi) {}

    Program parse();

private:
    struct Local {
        std::string name;
        int offset;
        const Type *type;
        bool isConst = false;
        std::string staticName;
        bool isRegister = false;
        // [expr.const]: a const object of integral type initialised with a constant
        // expression *is* one, so its value has to be kept where fold() can find it.
        // The object still has an address; this is what it is worth when read.
        bool isConstantValue = false;
        long long constantValue = 0;
        // **And the same for a floating one.**
        bool isConstantDouble = false;
        long double constantDouble = 0;
        // **[class.copy]/31 elides a `return` copy, and excludes a parameter.**
        bool isParameter = false;
        // A by-value class parameter that arrived by address is lowered to a reference, so by its slot alone it looks like `T &t`.
        bool byValueByAddress = false;
        // **[stmt.dcl]/3: a jump may not enter this object's scope.** Set for an
        // automatic object with an initialiser, a constructor or a destructor - the
        // three things a jump landing past its declaration would skip.
        bool guardsJump = false;
    };

    struct GlobalSym {
        std::string name;
        std::string symbol;
        const Type *type;
        bool isConst = false;
        bool emitted = false;
        bool hasInit = false;
        bool isConstantValue = false;   // as for Local, above
        long long constantValue = 0;
        bool isConstantDouble = false;  // and the floating one, as for Local
        long double constantDouble = 0;
    };

    struct Signature {
        std::string name;
        std::string symbol;
        const Type *returns;
        std::vector<const Type *> params;
        bool variadic;
        bool defined;
        std::size_t pos;
        // A name with C linkage, and `main`, carry one symbol and so can hold one function.
        bool cLinkage;
        // The class this belongs to, empty for a free function. A member is
        // keyed in the table as "Point::get", so overload resolution works on
        // members with no second implementation of it.
        std::string owner;
        bool constThis = false;
        Access access = Access::Public;
        bool isVirtual = false;
        // **`explicit` on a constructor**, which changes nothing about the function and only about who may pick it.
        bool isExplicit = false;
        // **A static member function is a member in every way but the one that matters at a call**.
        bool isStaticMember = false;
        // **`noexcept` on this function**, which in C++11 is not part of its type.
        bool isNoexcept = false;
        // **Nobody wrote this one.** An implicitly declared special member is put in the
        // table so overload resolution finds it, and given a body only if something
        // calls it - which is what keeps the symbol list level with the oracles.
        bool implicit = false;
        bool used = false;
        // **A specialization is a candidate like any other, and loses a tie.**
        // [over.match.best]: where a specialization and an ordinary function are equally
        // good, the ordinary one wins. It is also never a redeclaration of one.
        bool fromTemplate = false;
    };

    // One vtable slot: the function it currently points at, and enough of the
    // declaration to tell an override from an unrelated function of the same name.
    // Slots keep the order the base first declared them in, so the views agree.
    struct VSlot {
        std::string name;
        std::string symbol;
        std::vector<const Type *> params;
        bool constThis;
        // **A pure virtual holds a slot it has no function for.** The entry is
        // the runtime's own trap, and a class with one of these still in its
        // table is abstract.
        bool pure = false;
        const Type *returns = nullptr;     // checked when an override takes the slot
    };
    std::map<std::string, std::vector<VSlot> > vtables_;
    void checkOverrideReturn(const VSlot &s, const Type *returns, const std::string &cls,
                             const std::string &name, std::size_t pos) const;
    // Where a class's own new slots begin, after its primary base's - the Microsoft overload order is kept within that run only.
    std::map<std::string, std::size_t> ownSlotsFrom_;
    // Whether a slot is the one a member with this name and parameter list overrides.
    // Three places ask it - the slot search, the search across the bases after the
    // first, and the secondary table's walk - and the third copy is why it is named.
    static bool overrides(const VSlot &s, const std::string &name,
                          const std::vector<const Type *> &params,
                          bool constThis);
    // Where a class's secondary vptr for a given base points into its table, keyed "Derived::Base".
    std::map<std::string, int> secondaryVptr_;

    // **cl's hidden most-derived flag, and who wants it.**
    std::set<std::string> msVbaseCtors_;
    // The tags of those classes, for the destructor side.
    std::set<std::string> msVbaseClasses_;
    // Where the flag sits in the constructor being emitted, -1 for a function that has none.
    int msVbInitSlot_ = -1;
    // A member function body written inside the class, held until the class closes.
    // `start` is the token the whole declaration begins at, so the replay re-reads the
    // return type and parameters too rather than trying to reconstruct them.
    struct PendingBody {
        std::string tag;
        std::size_t start;
        // The class's name **as the source wrote it**, which is not the tag for a specialization.
        std::string local;
        // The function table's key for the member this body belongs to.
        std::string key;
        // **And which overload under that key**, because a constructor shares its key with every other constructor of the class.
        std::size_t which;
        // **A friend defined inside the class is not a member of it.**
        bool freeFunction = false;
        static std::size_t npos() { return (std::size_t)-1; }
    };
    std::vector<PendingBody> pendingBodies_;
    // **Where a declaration actually put its signature.**
    std::size_t signatureAddedUnder(const std::string &key,
                                    std::size_t before) const {
        const std::vector<std::size_t> *set = overloadsOf(key);
        if (set == nullptr || set->empty()) return PendingBody::npos();
        return set->back() >= before ? set->back() : PendingBody::npos();
    }
    void replayInlineBodies(std::vector<PendingBody> mine);
    // **[dcl.inline]/6: a member defined inside its class is implicitly
    // inline**, and so is every member of a template specialization - which is
    // to say, everything that reaches `topLevel` through a *replay*.
    bool replayingInline_ = false;
    void skipBracedBlock();

    // ---- Rung 5.1: the template table, and nothing instantiated ----
    // One template parameter as written: `class T` / `typename T`, or a non-type one
    // such as `int N`. Nothing is bound to either yet.
    struct TemplateParam {
        std::string name;
        const Type *type = nullptr;   // null for a type parameter
        std::size_t pos = 0;
        // `class...`
        bool isPack = false;
        // A default argument, kept as a half-open token range [begin, end) the way a default
        // *function* argument is - instantiation replays tokens, so a default is replayed too, with
        // the earlier parameters bound. begin == 0 means there is none.
        std::size_t defBegin = 0;
        std::size_t defEnd = 0;
    };
    // What is known about a name that names a template. `start` is the
    // `template` keyword: instantiation here will be a replay of these
    // tokens, so where they begin is the thing worth keeping.
    struct TemplateDecl {
        std::string name;
        // The namespace it was declared in, `std::` included. Carried to the
        // specialization for the manglers alone - see Type::templateNamespace.
        std::string ns;
        std::vector<TemplateParam> params;
        bool isClass = false;
        // Written `class` rather than `struct`: a specialization made while the
        // template is only declared has to describe itself the way it was declared.
        bool classKey = false;
        bool defined = false;
        std::size_t start = 0;
        // The token after the `>` that closed the parameter list, which is
        // where the declaration proper begins. Instantiating is re-reading
        // from here with the arguments bound.
        std::size_t afterParams = 0;
        std::size_t pos = 0;
        // **A member of this class template defined outside it** - `template <class T>
        // T Box<T>::get(int)`. Kept on the class rather than as a template of its own,
        // because that is what it is. Replayed when the class is instantiated.
        struct OutOfLine {
            std::size_t start = 0;
            std::string member;    // "get", or the class's name for a ctor
            bool destructor = false;
            // **A static data member, not a member function.**
            bool isData = false;
        };
        std::vector<OutOfLine> outOfLine;

        // **A partial specialization: a second body for the argument lists that match a
        // pattern.** Its arguments are patterns rather than types, which is why they are
        // kept as written and matched at every use rather than resolved once.
        struct Partial {
            std::vector<TemplateParam> params;
            // One written argument. A type argument is a pattern; a non-type
            // one is either a value or one of this specialization's own
            // parameters, which is the only shape of it that deduces.
            struct Arg {
                bool isType = true;
                const Type *type = nullptr;
                bool isParam = false;
                std::size_t param = 0;
                long long value = 0;
                // `R...` written as the last argument of the pattern, where R is this
                // specialization's own pack. It takes every argument the fixed ones in
                // front did not, which lets `L<T, R...>` peel one type off any length.
                bool isPackExpansion = false;
            };
            std::vector<Arg> args;
            std::size_t bodyAt = 0;      // the '{' of its class body
            std::size_t pos = 0;
        };
        std::vector<Partial> partials;

        // ---- A member function template ----
        // `template <int M> CVector<M> head() const { ... }` written inside a
        // class.
        bool isMember = false;
        Access memberAccess = Access::Public;
        std::string ownerTag;
        const Type *ownerType = nullptr;
        std::vector<TemplateParam> classParams;
        std::vector<const Type *> classBinding;
        std::vector<long long> classValues;
    };
    std::map<std::string, TemplateDecl> templates_;
    // **Function templates overload; class templates do not.**
    std::map<std::string, std::vector<TemplateDecl> > fnTemplates_;
    // Try every function template named `name` against these argument types,
    // instantiating each that deduces - a substitution failure just drops that
    // one.
    void instantiateViableTemplates(const std::string &name,
                                    const std::vector<const Type *> &argTypes,
                                    std::size_t pos);
    // The same, for a member function template of `cls`.
    void instantiateViableMemberTemplates(const Type *cls,
                                          const std::string &name,
                                          const std::vector<const Type *> &argTypes,
                                          std::size_t pos);
    // Member function templates, keyed by "<ownerTag>::<member>".
    std::map<std::string, TemplateDecl> memberTemplates_;
    // Set one-shot while a member-template specialization's body is replayed through declareMember:
    // the source member name, the display name with its arguments ("head<3>"), and those arguments,
    // so declareMember keys and mangles the specialization rather than the plain member.
    bool memberTemplateInst_ = false;
    std::string memberTemplateOf_;
    std::string memberTemplateName_;
    std::vector<TemplateArg> memberTemplateArgs_;
    Access memberTemplateAccess_ = Access::Public;
    bool isMemberTemplate(const Type *obj, const std::string &name) const {
        return obj != nullptr &&
               memberTemplates_.count(obj->unqualified()->tag() + "::" + name) != 0;
    }
    ExprPtr memberTemplateCall(ExprPtr object, const Type *obj,
                               const std::string &name, std::size_t pos);
    const Signature *instantiateMemberTemplate(
        const TemplateDecl &mt, const std::vector<const Type *> &binding,
        const std::vector<long long> &values,
        const std::vector<TemplateArg> &args, std::size_t pos);
    // **A template is keyed by its bare name, and named by either spelling.**
    std::map<std::string, TemplateDecl>::const_iterator
    findTemplate(const std::string &written) const {
        std::map<std::string, TemplateDecl>::const_iterator it =
            templates_.find(written);
        if (it != templates_.end()) return it;
        const std::string::size_type cut = written.rfind("::");
        if (cut == std::string::npos) return templates_.end();
        return templates_.find(written.substr(cut + 2));
    }
    // Whether a name, either spelling, is a class template - which is the one
    // question the type and expression paths both have to ask.
    bool isClassTemplate(const std::string &written) const {
        std::map<std::string, TemplateDecl>::const_iterator it =
            findTemplate(written);
        return it != templates_.end() && it->second.isClass;
    }

    // ---- Rung 5.2: function templates, explicit arguments ----
    // One specialization that has been asked for. The body is not written where the
    // call is, so the request is recorded and the definitions are replayed afterwards.
    struct Specialization {
        std::string key;                    // "twice<int>", and the table key
        std::string name;                   // "twice"
        std::string symbol;
        const Type *fn = nullptr;           // the substituted signature
        std::vector<const Type *> binding;  // by parameter index
        std::vector<long long> values;      // non-type arguments, same index
        std::vector<std::vector<const Type *> > packs;   // same index again
        std::size_t start = 0;              // the template's own tokens
        std::size_t pos = 0;                // where it was first asked for
        bool emitted = false;
        // A class specialization instead of a function one.
        bool isClass = false;
        // Written out rather than made, so there are no parameters to bind and no primary template to replay.
        bool explicitly = false;
        // The parameters `binding` and `values` are for. Usually the
        // template's own; for a partial specialization they are its.
        std::vector<TemplateParam> params;
        std::vector<PendingBody> bodies;
        // Out-of-line definitions already turned into keys for this tag. They are
        // replayed through topLevel and not replayInlineBodies: the tokens carry
        // `Box<T>::get`, so the qualifier is written and the ordinary path reads it.
        struct Outside {
            std::size_t start = 0;
            std::string key;
        };
        std::vector<Outside> outside;
        // Which of the class template's out-of-line definitions have been replayed for
        // this specialization. Not a count: the list can grow after the class is made,
        // a definition being writable further down the file than the first use.
        std::vector<bool> outsideDone;
    };
    std::vector<Specialization> specializations_;
    // Set while a specialization's definition is being replayed: the name the
    // function must be given, and the symbol it must carry. One-shot, taken
    // by the declarator that names the function.
    std::string instantiationKey_;
    std::string instantiationOf_;
    // A `>` inside a template argument list closes it and is not an operator
    // - [temp.names], and the reason a comparison there must be parenthesised.
    // Cleared inside parentheses, where a `>` is an operator again.
    bool inTemplateArgs_ = false;
    const std::string &instantiationName(const std::string &name) const {
        return (!instantiationKey_.empty() && name == instantiationOf_)
                   ? instantiationKey_ : name;
    }
    ExprPtr templateCall(Program *program);
    // A static member reached through a class template-id, after its `::`.
    ExprPtr templateIdMember(const Type *cls, std::size_t pos);
    // `Box<int, 3>` where a type was expected.
    const Type *instantiateClass(const TemplateDecl &decl, std::size_t pos);
    // Set while a class template is being instantiated.
    std::string classInstantiationTag_;
    // The template's own name, for the explicit-specialization path.
    std::string classInstantiationOf_;
    // **Held bodies are deferred only for an implicit instantiation**, which happens in the middle of whatever asked for the class.
    bool deferSpecializationBodies_ = false;
    // `template <> struct Box<int> { ... };`.
    bool explicitSpecialization();
    // The arguments that tag was built from, and the bodies the class came back with.
    std::vector<TemplateArg> instantiatingArgs_;
    // The template's namespace, travelling the same short distance for the same reason.
    std::string instantiatingNamespace_;
    // The class template's own parameters and the arguments this instantiation
    // bound them to, travelling the same short distance so a member function
    // template declared in the body can capture them.
    std::vector<TemplateParam> instantiatingParams_;
    std::vector<const Type *> instantiatingBinding_;
    std::vector<long long> instantiatingValues_;
    std::vector<PendingBody> heldForSpecialization_;
    // Set while a template's declaration is read as a *pattern*, with Kind::TemplateParam in place of the arguments.
    bool patternOnly_ = false;

    // **A trial, and everything it has to put back.** Forming a candidate's signature
    // reads tokens, pushes classes and binds parameters; if it fails half way through,
    // none of that may be left behind for the next candidate to trip over.
    struct Trial {
        Parser *p;
        std::size_t at;
        std::size_t classes;
        bool pattern;
        // **The half-taken `>>` counts as state too.** `angleSplit_` marks the token
        // whose first `>` has been consumed, and a substitution that fails between the
        // halves would leave the outer list unterminated at the next reading.
        std::size_t split;
        explicit Trial(Parser *parser);
        ~Trial();
    };
    const Signature &instantiate(const TemplateDecl &decl,
                                 const std::vector<const Type *> &binding,
                                 const std::vector<long long> &values,
                                 const std::vector<TemplateArg> &args,
                                 std::size_t pos,
                                 const std::vector<std::vector<const Type *> > &packs =
                                     std::vector<std::vector<const Type *> >());
    void instantiatePending();
    bool memberIsUsed(const std::string &key) const;
    // "twice<int>" - the table key and what the diagnostics call it.
    std::string specializationKey(const std::string &name,
                                  const std::vector<TemplateArg> &args) const;
    // The template's declaration re-read with `binding` in force. Answers its
    // function type and, through `name`, the name it declares.
    const Type *readTemplateDeclaration(const TemplateDecl &decl,
                                        const std::vector<const Type *> &binding,
                                        const std::vector<long long> &values,
                                        std::string *name,
                                        std::string *qualifier = nullptr,
                                        const std::vector<std::vector<const Type *> > *packs = nullptr,
                                        bool *construction = nullptr);
    // What a binding does to the two name tables, and how to put them back.
    struct Shadow {
        std::string name;
        bool isType = true;
        bool isPack = false;
        bool had = false;
        std::size_t was = 0;
        std::vector<const Type *> hadPack;
        std::vector<std::string> hadNames;
        // A non-type parameter bound as a pattern reference rather than a value
        // - `N` in `V<N>` while a signature is read for deduction.
        bool isParamRef = false;
        bool hadParamRef = false;
        int paramRefWas = 0;
    };
    // **A pack while a specialization is being read**: the types it was given and, once
    // its function parameters have been made, the names they were given. `rest` becomes
    // `rest$0` and `rest$1`, and `rest...` in a call is those two names.
    struct PackBinding {
        std::vector<const Type *> types;
        std::vector<std::string> names;
    };
    std::map<std::string, PackBinding> packs_;
    // While a signature is read as a pattern for deduction, a non-type parameter is bound here.
    std::map<std::string, int> nonTypePatternParams_;
    // `Ts... rest` in a parameter list. Answers false where the tokens are
    // not that; otherwise reads it and adds what it expands to.
    bool packParameter(std::vector<const Type *> *types,
                       std::vector<std::string> *names);
    // `packs` is indexed like the others and read only for a parameter that
    // is one; everything else ignores it.
    void bindTemplateParameters(const std::vector<TemplateParam> &params,
                                const std::vector<const Type *> &binding,
                                const std::vector<long long> &values,
                                const std::vector<std::vector<const Type *> > &packs,
                                std::vector<Shadow> *undo);
    // [temp.deduct.type] rather than [temp.deduct.call]: this matches a template
    // *argument* against a pattern, where nothing decays and nothing may differ.
    // Deduction from a call is looser, because a conversion may still bridge it.
    bool matchPattern(const Type *pattern, const Type *arg,
                      std::vector<const Type *> *binding,
                      std::string *why) const;
    // The partial specialization these arguments ask for, or npos for none.
    // Refuses by name where two match and neither is more specialized.
    std::size_t choosePartial(const TemplateDecl &decl,
                              const std::vector<TemplateArg> &args,
                              std::vector<const Type *> *binding,
                              std::vector<long long> *values,
                              std::vector<std::vector<const Type *> > *packs,
                              std::size_t pos);
    // [temp.class.order]: whether one pattern is at least as specialized as
    // another, which is asked by matching each against the other.
    bool atLeastAsSpecialized(const TemplateDecl::Partial &a,
                              const TemplateDecl::Partial &b) const;
    bool moreSpecialized(const TemplateDecl::Partial &a,
                         const TemplateDecl::Partial &b) const;
    // `Box<T *>` after `template <class T> struct` - the argument list of a
    // partial specialization, read as patterns.
    void partialArguments(TemplateDecl::Partial *ps,
                          const std::vector<TemplateParam> &primary);
    void unbindTemplateParameters(const std::vector<Shadow> &undo);
    // Deduction: the arguments worked out from the call rather than written.
    // Answers false and fills `why` when some parameter cannot be deduced.
    bool deduceTemplateArguments(const TemplateDecl &decl,
                                 const std::vector<const Type *> &argTypes,
                                 std::vector<const Type *> *binding,
                                 std::vector<long long> *values,
                                 std::vector<std::vector<const Type *> > *packs,
                                 std::string *why);
    // `values` receives a deduced non-type argument (the 3 in V<3>); it may be
    // null where only type parameters can occur (the `auto` path).
    bool deduceOne(const Type *pattern, const Type *arg,
                   std::vector<const Type *> *binding,
                   std::vector<long long> *values, std::string *why) const;
    // An argument's type as a parameter sees it.
    const Type *decayedType(const Type *a) const;
    // ---- Rung 7.1: `auto` ----
    // [dcl.spec.auto] deduces a variable's `auto` **as if by template argument deduction
    // from a call**, so deduceOne does the work and Kind::Deduced stands in.
    static bool mentionsDeduced(const Type *t);
    const Type *substituteDeduced(const Type *t, const Type *with);
    // Reads the initialiser to learn its type, puts the tokens back, and
    // answers the declared type with `auto` replaced.
    const Type *deduceAuto(const Type *declared, const std::string &name,
                           std::size_t pos);
    // The same deduction against a type already in hand, for a `for` whose
    // initialiser is built rather than written.
    const Type *deduceAutoFrom(const Type *declared, const Type *from,
                               const std::string &name, std::size_t pos);
    // ---- Rungs 7.3 and 7.2: the range-based `for`, and `decltype` ----
    // A declaration followed by `:` takes a scan to tell from `for (int x = a?b:c;)`;
    // and [dcl.type.simple] separates `decltype(x)` from `decltype((x))` by shape.
    const Type *decltypeSpecifier();
    bool atNamePath() const;
    bool atRangeFor() const;
    StmtPtr rangeForStatement(int scope);
    // [stmt.ranged]'s begin-expr and end-expr for a class range, and the slots
    // holding them. Returns the iterator type; fills `setup` with the three
    // assignments the loop needs in front of it.
    const Type *classRangeEnds(ExprPtr range, std::size_t rpos,
                               std::vector<StmtPtr> &setup, int *bSlot,
                               int *eSlot, std::string *bName,
                               std::string *eName);
    // The argument list at a use: `<int, 3>` read into types and values.
    void templateArguments(const TemplateDecl &decl,
                           std::vector<const Type *> *binding,
                           std::vector<long long> *values,
                           std::vector<TemplateArg> *args,
                           std::vector<std::vector<const Type *> > *packs = nullptr);
    // **A name declared as an object is not a template name.**
    bool isTemplateName(const std::string &name, bool qualified = false) const {
        if (templates_.find(name) == templates_.end()) return false;
        if (qualified) return true;
        return findLocal(name) == nullptr && findGlobal(name) == nullptr;
    }
    bool templateDeclaration();
    void templateParameters(std::vector<TemplateParam> &params);
    // What the declaration after `template <...>` actually declares: a class template, a
    // function template, or a member of a class template defined outside it.
    // `qualifier` is the class for that last one and empty otherwise.
    std::string templatedName(const std::vector<TemplateParam> &params,
                              bool *isClass, std::string *qualifier,
                              bool *construction = nullptr);
    // `sawInit`, when given, says the declaration ended at a `;` having passed
    // an `=` at depth zero - which is a static data member of a class template
    // being *defined* out of line, not a member left undefined.
    bool skipTemplatedDefinition(bool *sawInit = nullptr);
    void skipTemplateArguments();
    // `Box<T>::Box(` or `Box<T>::~Box(`.
    bool atOutOfLineSpecial(std::string *what);
    // The whole of what a use of a template does in 5.1.
    void refuseTemplateId();

    // **A `>>` closes two argument lists, and the lexer hands it over as one token.**
    // Held bodies record absolute token indices, so nothing may be inserted: the first
    // `>` is taken by leaving this index behind, the second by advancing past it.
    std::size_t angleSplit_ = static_cast<std::size_t>(-1);
    bool atClosingAngle() const;
    void takeClosingAngle();

    // Set only while an inline body is being replayed.
    std::string inlineOwner_;
    // The same class as the source spells it.
    std::string inlineOwnerName_;

    // The _ZTI a throw or a catch names, emitting one for a class on the way.
    // Answers empty and fills `why` where this compiler cannot describe it.
    std::string typeInfoSymbolFor(const Type *t, std::size_t pos,
                                  std::string *why);
    // The flags word and a virtual base's vtable slot, both for the
    // `__vmi_class_type_info` a class with anything but a single base at
    // offset zero needs.
    static long long itaniumVmiFlags(const Type *cls);
    long long itaniumVbaseOffsetSlot(const Type *cls, const Type *vbase) const;
    std::string emitClassTypeInfo(const Type *cls, const std::string &tag,
                                  std::size_t pos);
    void emitVtable(const Type *cls, const std::string &tag, std::size_t pos);
    // The Microsoft vbtable: where each virtual base is, measured from the vbptr.
    void emitVbtable(const Type *cls, const std::string &tag, std::size_t pos);
    // **Emitting a vtable uses everything the table points at.**
    void markSymbolUsed(const std::string &symbol);

    // **Mark a signature that came out of `functions_` used, and the one place the pointer arithmetic this file warns about is written.**
    void markUsed(const Signature *f);
    // A vtable entry, a typeinfo field and the vptr: 8 bytes on the hosts, 4 on the C6000.
    int pointerBytes() const { return target_.sizeOf(Kind::Pointer); }
    // An offset stored among them (offset-to-top, a vbase_offset): as wide, and signed.
    const Type *wordOffsetType() const { return types_.get(pointerBytes() == 4 ? Kind::Int : Kind::LongLong); }
    // **How far the address point sits past the table's first byte.**
    int vtableHeaderBytes(const Type *cls) const {
        if (target_.microsoftNames()) return 0;
        int n = 0;
        const std::vector<Type::BaseSpec> &bs = cls->bases();
        for (std::size_t i = 0; i < bs.size(); i++) if (bs[i].isVirtual) n++;
        return (n + 2) * pointerBytes();
    }

    // Two parameter lists compared as C++ compares them - same length, same
    // types, in order. Six places asked it inline; what goes beside it, constThis
    // or variadic, stays with the caller, because the answers differ.
    static bool sameParameters(const std::vector<const Type *> &a,
                               const std::vector<const Type *> &b);

    // `(*this).m`, typed as the member is declared.
    ExprPtr thisMember(int thisSlot, const Type *cls, const Member &m);
    // Reaching a member that lives in a virtual base.
    void refuseVirtualBaseMember(const Type *staticType, const Member &m,
                                 const std::string &name, std::size_t pos);
    ExprPtr virtualBaseMember(ExprPtr object, const Type *staticType,
                              const Member &m);
    ExprPtr microsoftVirtualBaseMember(ExprPtr object, const Type *owner,
                                       const Member &m);
    // The bytes from an object to its virtual base, read out of the vbtable.
    // `temp`/`held` name a char * lvalue already holding the address.
    ExprPtr microsoftVirtualBaseStep(const Type *owner, const Type *vbase,
                                     const std::string &temp, int held,
                                     int *baseAt) const;
    // Which vbtable entry holds a virtual base, and where that base sits.
    // Entry 0 is the vbptr's own offset, so 0 back means "not in this table".
    static int virtualBaseSlot(const Type *owner, const Type *vbase,
                               int *baseAt);

    // A member the layout copied down from a base, told by the offset it sits at.
    static bool memberFromBase(const Type *cls, const Member &m);

    // **Would default-initialising this leave anything uninitialised?**
    bool constDefaultInitialisable(const Type *t) const;

    // [dcl.init]/7 for an object declared const with no initialiser at all.
    void requireConstInitialised(const Type *t, const std::string &name,
                                 std::size_t pos);
    // The statements that set an object's vptrs - the primary one and any a
    // polymorphic second base needs. Every constructor emits these, written
    // or implicit, which is why they live in one place.
    std::vector<StmtPtr> storeVptrs(const std::string &cls, const Type *memberOf,
                                    int thisSlot);
    // And the vbtable pointer beside them, Microsoft only.
    void storeVbptr(const std::string &cls, const Type *memberOf, int thisSlot,
                    std::vector<StmtPtr> &into);
    // The `if (mostDerived)` a Microsoft constructor opens with: the vbtable
    // pointers and the virtual bases, built only by the complete object.
    std::vector<StmtPtr> guardedVirtualBaseInit(
        const Type *type, const std::string &cls, int thisSlot, int flagSlot,
        std::size_t pos, int srcSlot, bool moving,
        std::map<std::string, std::vector<ExprPtr> > *vbaseArgs);
    // A thunk: the entry a secondary table holds for a function this class
    // overrides. It moves `this` back to the complete object and calls the
    // real one. Named for the offset it undoes - _ZThn16_N1C1gEv.
    std::string synthesizeThunk(const std::string &cls, const Type *type,
                                const VSlot &slot, int offset, std::size_t pos);

    // How well one argument matches one parameter, in the order [over.ics.scs]
    // ranks them - the values are compared, so do not reorder this enum.
    enum class Rank { Identity, Qualification, Promotion, Conversion,
                      UserDefined, Ellipsis, None };
    // `ranksObjectA`/`B` say whether that candidate's rank vector opens with an implicit object
    // parameter, which is how a rank position is mapped back to the parameter it came from - a
    // member and a non-member operator are ranked side by side and do not open the same way.
    bool betterCandidate(const std::vector<Rank> &a, const std::vector<Rank> &b,
                         const Signature &fa, const Signature &fb,
                         bool ranksObjectA, bool ranksObjectB) const;

    // The parameter a rank position came from, or null for an implicit object
    // parameter and for anything the ellipsis swallowed.
    static const Type *rankedParameter(const Signature &f, std::size_t i,
                                       bool ranksObject);

    // Whether two parameters are the same match either way round.
    static bool sameMatchEitherWay(const Type *pa, const Type *pb);

    struct Declared {
        std::string name;
        const Type *type;
        std::size_t pos;

        std::size_t paramsAt = 0;
        // The class named before a `::` in a definition's declarator -
        // `int Point::get()` declares nothing called "Point::get", it defines
        // the `get` that Point already declared.
        std::string qualifier;
    };

    enum StorageClass { StorageNone, StorageStatic, StorageExtern, StorageTypedef,
                        StorageRegister, StorageAuto };

    struct Qualifiers {
        bool isConst = false;
        bool isVolatile = false;
        // `constexpr` implies const on an object, so isConst is set with it and almost everything downstream needs to know nothing more.
        bool isConstexpr = false;
        // `inline` on a function: its definition may appear in several
        // translation units, so it is emitted as a weak/COMDAT definition the
        // linker folds - the same treatment a template specialization gets.
        bool isInline = false;
    };

    struct TypedefName {
        std::string name;
        const Type *type;
    };

    struct EnumConst {
        std::string name;
        long long value;
    };

    const Source &src_;
    std::vector<Token> tokens_;
    TypeTable &types_;
    const Target &target_;

    // Read here for one question - how a class comes back - and by each backend for the rest.
    const Abi &abi_;
    // **How a class goes to a function, and the two ABIs part company here.** Itanium
    // passes one by address whenever copying or destroying it is a call, and the caller
    // destroys the copy; Microsoft passes by the size rules and the callee destroys.
    bool passedByAddress(const Type *t) const {
        if (!t->isStructOrUnion()) return false;
        if (t->nonTrivialCopy()) return true;
        return !target_.microsoftNames() && t->hasDestructor();
    }
    bool returnsIndirectly(const Type *t, bool memberFn = false) const {
        int size = t->size(target_);

        // A class whose copy or destruction is a call is returned through a
        // hidden pointer whatever its size - the caller owns the storage and
        // the callee builds into it. Both ABIs agree here, measured.
        if (t->nonTrivialCopy() || t->hasDestructor()) return true;
        if (containsX87(t, target_)) return true;
        if (abi_.aggregatesByReference) {
            // **The Microsoft size rule is for free functions only.** A class returned
            // by value from a non-static member goes through the hidden pointer whatever
            // its size - measured with clang for this ABI, %rdx against %eax.
            if (memberFn) return true;
            return !(size == 1 || size == 2 || size == 4 || size == 8);
        }
        if (abi_.homogeneousFloatAggregates) {
            Kind elem;
            if (homogeneousFloatCount(t, &elem) > 0) return false;
        }
        return size > abi_.structReturnLimit;
    }
    bool variadicBody_ = false;

    std::size_t at_ = 0;

    std::vector<Local> locals_;

    std::vector<::Local> fnVars_;
    bool inParams_ = false;
    std::vector<std::size_t> scopeStarts_;
    std::vector<int> blocks_;
    std::vector<int> blockStack_;

    bool atFunctionBody_ = false;
    int frameSize_ = 0;
    const Type *returnType_ = nullptr;
    // Set while a lambda's body is read to find its return type: a `return`
    // writes the type of its operand here, the first one deciding, and is
    // not checked against returnType_, there being none yet.
    const Type **deducingReturn_ = nullptr;
    std::string functionName_;
    std::vector<std::string> staticSymbols_;

    std::vector<Signature> functions_;
    // One name, every function declared under it. C had one; C++ has a set,
    // and the set is ordered by declaration so a diagnostic can list the
    // candidates in the order the reader wrote them.
    std::vector<Signature> &functionTable() { return functions_; }
    std::unordered_map<std::string, std::vector<std::size_t> > functionIndex_;
    std::vector<GlobalSym> globals_;
    std::unordered_map<std::string, std::size_t> globalIndex_;
    std::vector<TypedefName> typedefs_;
    std::unordered_map<std::string, std::size_t> typedefIndex_;
    std::vector<EnumConst> enums_;
    std::unordered_map<std::string, std::size_t> enumIndex_;
    int strings_ = 0;
    int loopDepth_ = 0;
    int switchDepth_ = 0;
    int caseIds_ = 0;

    // Objects that have been constructed and not yet destroyed - the entries
    // of alive_, below, innermost last.
    struct Alive {
        std::string name;
        int offset;
        const Type *cls;
        // Set for a by-value class parameter that arrived by address: its slot
        // holds the caller's pointer, so the object's address is what the slot
        // *contains* rather than where the slot sits.
        bool byAddress = false;
    };

    // One automatic object a jump may not land past - see Local::guardsJump. The frame
    // slot is the identity, since no two objects of one function share one and a name
    // can be declared again in an inner block; the name is for the message.
    struct JumpGuard { std::string name; int offset; };
    std::vector<JumpGuard> jumpGuards() const;
    void checkJump(const std::vector<JumpGuard> &from,
                   const std::vector<JumpGuard> &to, std::size_t pos,
                   const std::string &jump, const std::string &origin) const;

    struct SwitchCtx {
        std::vector<const Case *> cases;
        const Case *deflt;
        const Type *governing;
        // What was in scope at the `switch` itself, which is where every one
        // of its case labels is jumped to from.
        std::vector<JumpGuard> guards;
    };
    std::vector<SwitchCtx> switches_;

    // A label or a goto, with what was in scope where it was written.
    struct LabelDef {
        std::string name;
        std::size_t pos;
        std::vector<JumpGuard> guards;
        // The objects alive there - a copy of alive_. A goto holding one its label does
        // not is a jump out of that object's scope and destroys it on the way: the calls
        // go into `cleanups`, filled by resolveGotos(). Null for a label.
        std::vector<Alive> alive;
        Block *cleanups;
    };
    // alive_.size() at the entry to each loop body, and to each loop body
    // or switch body: what `continue` and `break` respectively destroy on
    // the way out is everything built since.
    std::vector<std::size_t> loopMarks_;
    std::vector<std::size_t> breakMarks_;
    StmtPtr jumpLeaving(StmtPtr jump, std::size_t mark, std::size_t pos,
                      int endsCatches);
    std::vector<LabelDef> labels_;
    std::vector<LabelDef> gotos_;

    const Token &peek() const { return tokens_[at_]; }
    const Token &peekAt(std::size_t n) const;
    bool consume(const char *s);
    void expect(const char *s);
    std::string expectIdent(const char *what);
    long long expectNumber(const char *what);

    bool atTypeName() const;
    const Type *findTypedef(const std::string &name) const;
    // **`::Lexer` - the global scope and nothing nearer**, which is the whole
    // of what a leading `::` asks for.
    const Type *findGlobalTypedef(const std::string &name) const;
    void declareTypeName(const std::string &name, const Type *type);
    const EnumConst *findEnum(const std::string &name) const;
    const Type *memberTypeWalk(const Type *t);
    const Type *structOrUnionSpecifier(Kind kind, bool isClass = false);
    void checkAccessible(const Type *object, const Member &m, std::size_t pos) const;
    // A constructor is keyed in the function table under "Point::Point", so resolving
    // one is resolving an overload set like any other. And the last component of a
    // qualified tag: "Outer::Inner" is "Inner", which is what those two are written as.
    static std::string localOf(const std::string &qualified) {
        const std::size_t at = qualified.rfind("::");
        return at == std::string::npos ? qualified : qualified.substr(at + 2);
    }
    static std::string constructorKey(const std::string &cls) {
        return cls + "::" + localOf(cls);
    }
    void declareConstructor(const std::string &cls, std::size_t pos, Access access,
                            bool isExplicit);
    void declareDestructor(const std::string &cls, std::size_t pos, Access access,
                           bool isVirtual);
    std::string deletingDestructorSymbol(const std::string &cls);
    void registerDestructor(const std::string &cls, std::size_t pos,
                            Access access, bool isVirtual, bool implicit);
    void declareImplicitDestructor(const std::string &tag, const Type *type,
                                   std::size_t pos);
    void synthesizeDestructor(std::size_t which);
    // [class.dtor]/8's member half, shared by the destructor the compiler
    // writes and the one the program writes.
    std::vector<StmtPtr> memberDestructors(const std::string &cls,
                                           const Type *type, int thisSlot,
                                           std::size_t pos);
    // The deleting destructor, which no program writes: it runs the
    // destructor and then frees. Itanium calls it D0 and Microsoft ??_G, and
    // it is what a `delete` through a base pointer reaches.
    void synthesizeDeleting(const std::string &cls, const Type *type,
                            Access access, std::size_t pos);
    // **A class with a virtual base needs two bodies where every other class
    // needs one.**
    void synthesizeCompleteCtor(const Type *type,
                                const std::vector<const Type *> &ctorParams,
                                const std::string &c1, const std::string &c2,
                                bool isInline, std::size_t pos,
                                int copyArg = -1, bool moving = false,
                                std::map<std::string, std::vector<ExprPtr> >
                                    *vbaseArgs = nullptr);
    // The same split on the way out, and the order reverses with it: D1 calls
    // D2 - which destroys what C2 built - and then destroys the virtual bases.
    void synthesizeCompleteDtor(const Type *type, const std::string &d1,
                                const std::string &d2, bool isInline,
                                std::size_t pos);
    // The construction calls for a class's virtual bases, reached by the
    // constant offset this class laid them down at. Shared by both of the
    // above and by the implicit members, which need the identical walk.
    std::vector<StmtPtr> virtualBaseCalls(const Type *type, int thisSlot,
                                          bool building, std::size_t pos,
                                          int srcSlot = -1, bool moving = false,
                                          std::map<std::string,
                                              std::vector<ExprPtr> >
                                              *vbaseArgs = nullptr);
    static std::string destructorKey(const std::string &cls) {
        return cls + "::~" + localOf(cls);
    }
    // A constructor of this class taking nothing, written or implicit.
    const Signature *defaultConstructorOf(const Type *cls) const;
    // What the class did not write, the compiler declares. Called once the
    // class is complete, because whether an implicit member is trivial - and
    // so whether it is a function at all - is a question about the members.
    void declareImplicitSpecials(const std::string &tag, const Type *type,
                                 std::size_t pos);
    // Bodies for the implicit members something actually called, run at the
    // end of the file and to a fixed point: giving one class a body can be
    // what first calls another's.
    void defineImplicitFunctions();
    void synthesizeDefaultCtor(std::size_t which);
    // One body for both halves of the copy. They differ in three places -
    // which member function to call, whether the vptr is stored, and whether
    // there is a value to return - and in nothing else.
    void synthesizeCopy(std::size_t which, bool assigning);
    const Signature *copyAssignOf(const Type *cls) const;
    void declareImplicitCopyAssign(const std::string &tag, const Type *type,
                                   std::size_t pos);
    static std::string assignmentKey(const std::string &cls) {
        return cls + "::operator=";
    }
    void declareImplicitCopyCtor(const std::string &tag, const Type *type,
                                 std::size_t pos);
    // A constructor of this class taking one reference to it, written or
    // implicit - which is what [class.copy] calls a copy constructor.
    const Signature *copyConstructorOf(const Type *cls) const;
    const Signature *moveConstructorOf(const Type *cls) const;
    void declareImplicitMoveCtor(const std::string &tag, const Type *type,
                                 std::size_t pos, bool userDeclared);
    // `int i = 0; while (i < count) { one; i = i + 1; }` over an array member's elements.
    StmtPtr eachElement(int indexSlot, long long count, StmtPtr one);
    // The name a base subobject's constructor is called by: Itanium's C2
    // rather than the C1 the signature carries, and on Windows the one name
    // there is.
    std::string baseConstructorSymbol(const Signature &ctor, const Type *base);
    const Signature *destructorOf(const Type *cls) const;
    // **Whether a class is a POD for layout**, which decides tail-padding reuse.
    bool podForLayout(const Type *t) const;
    // [class.mem]/1: one member of each name in a class, bases apart.
    void refuseDuplicateMember(const std::vector<Member> &members,
                               std::size_t ownFrom, const std::string &name,
                               const std::string &tag, std::size_t pos);
    ExprPtr destructorCall(ExprPtr address, const Signature &dtor, std::size_t pos);
    void destroyObject(std::vector<StmtPtr> &into, const Alive &a, std::size_t pos);

    // Objects that have been constructed and not yet destroyed, innermost last.
    std::vector<Alive> alive_;
    // Temporaries this full expression has made for by-value class arguments.
    struct Temporary {
        int slot;
        const Type *type;
        int flag;
        // **The storage `__cxa_allocate_exception` handed back**, which is a
        // temporary of the throw's own full expression and is released with
        // `__cxa_free_exception` rather than destroyed.
        bool exceptionStorage = false;
    };

    // **Everything that belongs to the function currently being parsed.** cxx1
    // re-enters parsing from a saved token index in eight places.
    struct FunctionState {
        std::size_t at = 0;
        std::string currentFunction, currentFunctionName, functionName;
        std::map<std::string, const Type *> localTypes;
        std::vector<Local> locals;
        std::vector<::Local> fnVars;
        std::vector<std::size_t> scopeStarts;
        std::vector<int> blocks, blockStack;
        std::vector<LabelDef> labels, gotos;
        int frameSize = 0, thisOffset = 0;
        const Type *currentClass = nullptr;
        const Type *returnType = nullptr;
        const Type **deducingReturn = nullptr;
        int lambdaCount = 0;
        bool atFunctionBody = false;
        std::vector<Alive> alive;
        std::vector<Temporary> pendingTemps;
        std::size_t bodyCleanupFrom = 0;
        bool functionHasPads = false, functionHasTry = false;
        std::vector<std::string> functionTypes;
        bool variadicBody = false, inStaticMember = false, inParams = false;
        std::vector<std::string> staticSymbols;
        std::vector<std::size_t> pendingDefaults;
        bool pendingNoexcept = false;
        bool inNoexceptFunction = false;
        bool replayingInline = false;
        // The six a review found still missing, each with a program that
        // reached it: `break` and `case` written in a lambda inside a loop or
        // a switch were accepted and segfaulted the compiler.
        int loopDepth = 0, switchDepth = 0;
        std::vector<SwitchCtx> switches;
        std::vector<std::size_t> breakMarks;
        bool inTryBody = false, inMsHandler = false, inHandlerBody = false;
        int handlerDepth = 0;
        std::vector<int> handlerLoopDepth, handlerSwitchDepth;
        std::vector<std::size_t> handlerFrom;
        std::string handlerResumeLabel;
        int handlerResumePtr = 0, handlerResumeSel = 0;
        int mayThrow = 0;
        bool conditionDecl = false;
        std::string conditionName;
        // Block-scope, and a lambda's body is its own block: a using-directive
        // written in one reached the end of the enclosing function.
        std::vector<std::string> usingNamespaces;
    };
    // **A parse whose result is discarded must put back what it accumulated.**
    struct Discarded {
        Parser *p;
        std::vector<Temporary> temps;
        std::vector<Alive> alive;
        explicit Discarded(Parser *pp)
            : p(pp), temps(pp->pendingTemps_), alive(pp->alive_) {}
        ~Discarded() { p->pendingTemps_ = temps; p->alive_ = alive; }
        Discarded(const Discarded &) = delete;
        Discarded &operator=(const Discarded &) = delete;
    };

    FunctionState captureFunctionState() const;
    void restoreFunctionState(const FunctionState &s);
    void clearFunctionState();

    // `except` is the frame offset of an object not to destroy - the one being returned,
    // which the caller destroys instead. And a cleanup region's landing pad: destroy
    // alive_[from..to), last first, and hand the exception back to the unwinder.

    // A pad's steps as a block, all but the last - the resume, or the jump
    // that hands over - in an inner block marked as running while the
    // exception unwinds: the Itanium unwinder continues from the resume call's own site.
    static StmtPtr unwindPad(std::vector<StmtPtr> steps);
    StmtPtr cleanupPad(std::size_t from, std::size_t to, int pointerSlot,
                       const std::vector<Temporary> &temps,
                       std::size_t pos,
                       const std::string &chainLabel = std::string());
    // The block's statements with each stretch that has objects alive turned
    // into a cleanup region of its own.
    std::vector<StmtPtr> wrapMsCleanups(
        std::vector<StmtPtr> body,
        const std::vector<std::pair<std::size_t, std::size_t> > &built,
        std::size_t aliveAtEntry, std::size_t pos,
        const std::vector<Temporary> &temps = std::vector<Temporary>());
    // `tryAt` names the statements a region must not span: a `try` is a row of
    // its own in the same table and destroys what it found alive itself.
    std::vector<StmtPtr> wrapCleanups(
        std::vector<StmtPtr> body,
        const std::vector<std::pair<std::size_t, std::size_t> > &built,
        std::size_t aliveAtEntry, std::size_t pos,
        const std::vector<Temporary> &temps = std::vector<Temporary>(),
        const std::vector<std::size_t> &tryAt = std::vector<std::size_t>());
    // `to` bounds the top of the range, for a cleanup pad that must destroy
    // only what existed at its point in the block.
    void emitDestructors(std::vector<StmtPtr> &into, std::size_t from,
                         std::size_t pos, int except = -1,
                         std::size_t to = static_cast<std::size_t>(-1));
    // `valueInit` is the `{}` half of [dcl.init]/8: the object is zeroed before
    // the constructor runs, and only where nobody wrote that constructor.
    StmtPtr constructLocal(const Declared &d, int offset,
                           std::vector<ExprPtr> args, bool copyInit = false,
                           bool valueInit = false);
    // The same construction into an object named by `symbol` when one is
    // given and by a frame slot otherwise - a static local, a file-scope
    // object and a static data member are the first kind.
    StmtPtr constructObject(const Declared &d, const std::string &symbol,
                            int offset, std::vector<ExprPtr> args,
                            bool copyInit, bool valueInit);
    ExprPtr objectAt(const Declared &d, const std::string &symbol, int offset);

    // **What a class with constructors is initialised from** - `(args)`,
    // `= expr`, `{}`, `= {}`, or a braced list for an initializer_list
    // constructor - shared by every storage duration, so it is written once.
    struct CtorInit {
        std::vector<ExprPtr> args;
        std::vector<StmtPtr> ilSetup;
        bool copyInit = false;
        bool valueInit = false;
        bool listInit = false;
        // A same-class source and no copy constructor: the bytes are asked for.
        bool trivialCopy = false;
    };
    CtorInit readConstructorInitialiser(const Declared &d);

    // **Dynamic initialisation** - [basic.start.init]/2 and [stmt.dcl]/4. The
    // statements of _GLOBAL__sub_I_<file>, its frame and its guards, built as
    // objects are met; parse() makes them one function the backends register.
    std::vector<StmtPtr> dynInit_;
    int dynInitFrame_ = 0;
    std::vector<int> dynInitGuards_;
    std::string initFunctionSymbol() const;
    // Everything below runs inside the init function's frame. The state
    // struct is what every other re-entry door saves, and this is one more.
    FunctionState enterInitFunction();
    void leaveInitFunction(const FunctionState &outer);
    void finishDynamicInit(Program &program);
    // The construction of a class object with static storage duration and its
    // destructor's registration, in the frame in force: [basic.start.term]
    // through __cxa_atexit, or atexit and a helper on the Microsoft ABI.
    std::vector<StmtPtr> buildStaticConstruction(const Declared &d,
                                                 const std::string &symbol,
                                                 const std::string &helper);
    void registerDestruction(const Declared &d, const std::string &symbol,
                             const std::string &helper,
                             std::vector<StmtPtr> &into);
    std::string atexitHelperName(const std::string &object) const;
    ExprPtr functionAddress(const std::string &symbol, const Type *fnType);
    // A file-scope object with a constructor, and a static data member of one.
    void dynamicInitialise(const Declared &d, const std::string &symbol,
                           const std::string &helper, bool once);
    // [stmt.dcl]/4.
    StmtPtr guardOnce(const std::string &symbol, std::vector<StmtPtr> body);
    // Where guardSlots_ stood when the init function's frame was entered.
    std::size_t initGuardMark_ = 0;
    void staticLocalWithConstructor(const Declared &d,
                                    std::vector<StmtPtr> &inits);
    std::string uniqueStaticSymbol(const std::string &name);
    // A reference with static storage duration: bound statically where the
    // initialiser is the address of a global, and before main otherwise.
    void bindStaticReference(const Declared &d, const std::string &symbol,
                             std::vector<GlobalPiece> &pieces, bool &hasInit,
                             std::vector<StmtPtr> *into);

    // A static data member: declared inside the class, defined outside it,
    // and reached by all three of `C::n`, `obj.n` and `p->n`.
    std::string staticMemberSymbol(const std::string &cls, const std::string &name,
                                   const Type *t, Access access, std::size_t pos);
    // Whether a call to this function has to be given an object. A member's
    // `owner` used to answer it on its own, and a static member is exactly the
    // case where the two questions come apart.
    static bool needsThis(const Signature &sig) {
        return !sig.owner.empty() && !sig.isStaticMember;
    }
    // Whether any function under this key is a static member. A call written
    // `C::f(...)` means one only if it is - `Base::f(...)` on an ordinary
    // member is a different construct and must not be taken for this one.
    bool hasStaticMemberNamed(const std::string &key) const {
        const std::vector<std::size_t> *set = overloadsOf(key);
        if (set == nullptr) return false;
        for (std::size_t i = 0; i < set->size(); i++)
            if (functions_[(*set)[i]].isStaticMember) return true;
        return false;
    }
    void declareStaticMember(const std::string &cls, Type *owner,
                             const Declared &d, Access access, bool volatileWritten);
    void defineStaticMember(Declared &d, Program &program);
    // **What a static member *defined* out of line is worth when read** - the
    // [expr.const]/3 read-back a namespace-scope const already makes.
    struct StaticConst {
        bool known = false;   long long value = 0;
        bool dknown = false;  long double dvalue = 0;
    };
    std::map<std::string, StaticConst> staticConsts_;
    const StaticConst *findStaticConst(const std::string &symbol) const {
        std::map<std::string, StaticConst>::const_iterator i =
            staticConsts_.find(symbol);
        return i == staticConsts_.end() ? nullptr : &i->second;
    }
    ExprPtr staticMemberRef(const Type *owner, const Type::StaticMember &s,
                            const std::string &cls, std::size_t pos);
    void declareMember(const std::string &cls, const Declared &d, bool constThis,
                       Access access, bool inUnion, bool isVirtual,
                       bool isStatic = false, bool isPure = false);
    // The entry a pure virtual's slot holds: the runtime routine that reports
    // a call reaching a function the class never defined. Both measured -
    // `__cxa_pure_virtual` from clang, `_purecall` from cl.
    void checkNotAbstract(const Type *t, std::size_t pos, const std::string &what);
    const char *pureVirtualSymbol() const {
        return target_.microsoftNames() ? "_purecall" : "__cxa_pure_virtual";
    }
    std::string memberSymbol(const std::string &cls, const std::string &name,
                             const Type *fn, Access access, bool constThis,
                             std::size_t pos, bool isVirtual = false,
                             bool isStatic = false);
    // `S a[4];` - the default constructor once per element. Separate from
    // constructLocal because that one builds a single object and names the
    // class through d.type, which for an array is the array.
    StmtPtr constructLocalArray(const Declared &d, int offset, int indexSlot);
    ExprPtr memberCall(ExprPtr object, const Type *cls, const std::string &name,
                       std::size_t pos);
    // `forceOwner` names the class a *qualified* call reached - `b.Base::f()`
    // or, inside a member, `Base::f()`.
    ExprPtr memberCallWith(ExprPtr object, const Type *cls,
                           const std::string &name, std::size_t pos,
                           std::vector<ExprPtr> args,
                           const Type *forceOwner = nullptr);
    // The class up the chain that declares this member function, searching every base rather than only the first.
    const Type *findMemberOwner(const Type *cls, const std::string &name) const;

    // The class whose member function is being parsed, and the frame offset of its
    // hidden `this` parameter; empty and 0 outside one. Below it, the classes whose
    // bodies are being parsed, outermost first, which gives a nested class its tag.
    std::vector<const Type *> classStack_;
    const Type *lookupInClass(const Type *cls, const std::string &name) const;
    // Whether the class being parsed, or the one whose member function is being parsed, is this class or something derived from it.
    bool insideClass(const Type *cls) const;
    // **[class.access]/6**.
    bool definesMemberOf(const Type *cls, std::size_t from) const;
    // `Point::Point(` and `Outer::Inner::~Inner(` have no type before the
    // name and the name IS a type, so the specifier list has to decline them.
    bool atUntypedMemberDefinition() const;
    const Type *currentClass_ = nullptr;
    int thisOffset_ = 0;
    const Type *enumSpecifier();
    bool atDeclarationStart() const;
    // Where a written qualified type name ends, **past a template argument list**.
    std::size_t qualifiedTypeEndPastArgs() const;
    // `volatile` is read and dropped, which is honest on an object and not
    // once the qualifier is under a `*`, a `&`, or on `this`. These two say so
    // by name rather than letting a wrong linkage name out.
    void refuseVolatilePointer();
    bool sawVolatile();
    void refuseVolatileWithLinkage(bool written, bool internal, std::size_t pos);
    void refuseVolatileUnderADeclarator(StorageClass storage);
    const Type *specifiers(StorageClass *storage, Qualifiers *quals = nullptr);
    const Type *unqualifiedSpecifiers(StorageClass *storage, Qualifiers *quals);

    Declared declarator(const Type *base, bool nameOptional = false,
                        bool insideParens = false);
    // `operator+` where a declarator wants a name: the whole of it is the name, so every
    // table keys it as it keys `get`. Then a lambda - rung 7.6 - and `P(1)`, a class
    // temporary built into a slot of this frame.
    ExprPtr classTemporary(const Type *cls, std::size_t pos);
    ExprPtr lambdaExpression();
    // [expr.prim.lambda]/4 with no trailing return type: a body that is one
    // `return expression;` has that expression's type, and anything else is void. The
    // expression is read with the parameters in scope and then put back, as 7.1 does.
    const Type *deduceLambdaReturn(std::size_t paramsFrom, std::size_t paramsTo,
                                   std::size_t bodyFrom, std::size_t bodyTo,
                                   const std::vector<std::string> &capNames,
                                   const std::vector<const Type *> &capTypes);
    int lambdaCount_ = 0;
    // Closures met while a body is read for its return type, counted apart.
    int deducedClosures_ = 0;
    // A closure's return-type typedef needs a name no other lambda will take.
    int lambdaRetSeq_ = 0;
    // The class a closure captured `this` from, by closure tag.
    std::map<std::string, const Type *> closureOuter_;
    // The class a lambda was written in, by closure tag, `this` captured or
    // not: [expr.prim.lambda]/7 puts the body in that member function's scope,
    // so its statics, enumerators and nested types are named bare.
    std::map<std::string, const Type *> closureScope_;
    const Type *lambdaScope() const;
    // `$this` - the member a `[this]` capture holds.
    static const char *capturedThis() { return "$this"; }
    // The class the enclosing function's `this` points at, const and all.
    const Type *capturedThisClass();
    // Is this name one of the enclosing class's own.
    bool namesOwnMember(const std::string &name);
    // Inside a closure that captured `this`, the pointer it holds.
    ExprPtr capturedThisPointer();
    // Does the code being parsed have a member's-eye view of this class?
    bool insideAccessOf(const Type *cls,
                        Access access = Access::Private) const;
    // **Accessibility asked from a named class rather than from the one being parsed.**
    bool accessibleFrom(const Type *from, const Type *owner, Access a) const;
    // A name that is a *capture of the lambda around this one*.
    ExprPtr outerCaptureAccess(const std::string &name);
    // **One closure type per lambda written, however often it is read.** 7.1 reads an
    // `auto` initialiser twice, so a lambda reached here twice and made two classes, and
    // the declaration then refused its own initialiser. Keyed by the '[' token.
    struct MadeLambda {
        const Type *type;
        std::size_t end;
        // The captures have to be copied in on *every* reading, not only the
        // one that built the class - the second reading was handing back an
        // uninitialised closure and the lambda saw whatever was on the stack.
        std::vector<std::string> names;
        std::vector<const Type *> types;
        std::vector<int> offsets;
    };
    // One closure object: a frame slot, and each capture copied into it.
    ExprPtr buildClosure(const MadeLambda &made, std::size_t pos);
    std::map<std::size_t, MadeLambda> lambdaAt_;

    std::string operatorName();
    // **The type a conversion function converts to**, set by operatorName when it reads one and read by the caller straight after.
    const Type *conversionTarget_ = nullptr;
    // Whether a member name is a conversion function's. The punctuation forms
    // are `operator+` with no space; this one is `operator bool`, with one.
    static bool isConversionName(const std::string &name) {
        return name.compare(0, 9, "operator ") == 0;
    }
    std::string declaredName(const char *what);

    // [class.friend].
    std::map<std::string, std::vector<std::string> > friends_;

    // The linkage name of the function whose body is being read, which is the whole of what an access check needs to ask about friendship.
    std::string currentFunction_;
    // The same function's *source* name, which a local class is qualified by.
    std::string currentFunctionName_;

    bool isFriendOf(const Type *cls) const;

    // [class.local]. A class defined in a function body belongs to that function: two
    // functions may each define `struct L` and they are different types. Both facts come
    // from these tables - a qualified tag, and a scope emptied when the function ends.
    std::map<std::string, const Type *> localTypes_;   // written name -> type
    std::map<std::string, std::string> localClassOwner_;  // tag -> enclosing symbol
    // The enclosing function's linkage name, for a class that is local to one.
    const std::string *localOwnerOf(const std::string &tag) const;
    // An operator this compiler can *name* but cannot yet reach from an expression is
    // refused where it is declared. The arity decides which - `operator-` is two
    // functions and only one is missing - so it is asked once the parameters are known.
    void checkOperatorDeclarable(const std::string &name,
                                 const std::vector<const Type *> &params,
                                 bool member, std::size_t pos);
    const Type *arraySuffix(const Type *base, std::size_t pos);
    const Type *promote(const Type *t) const;
    const Type *usualArithmetic(const Type *a, const Type *b) const;
    ExprPtr convert(ExprPtr e, const Type *to, bool allowExplicit = false) const;
    // [conv.mem]: a pointer to a member of B becomes one to a member of D, D
    // derived from B; the base's offset, or -1 where that is not the case.
    int memberPointerToDerived(const Type *from, const Type *to) const;
    ExprPtr convertMemberPointer(ExprPtr e, const Type *to, int shift) const;
    const Type *unsignedVersion(const Type *t) const;

    ExprPtr decay(ExprPtr e);
    void requireScalar(const Expr &e, std::size_t pos, const char *what);

    void checkAssignable(const Expr &from, const Type *to, std::size_t pos,
                         const std::string &what) const;

    int declare(const std::string &name, const Type *type, std::size_t pos);
    int allocateFrameSlot(const Type *type);
    void declareStaticLocal(const std::string &name, const Type *type,
                            std::size_t pos, const std::string &symbol);
    void requireAssignable(const Expr &e, std::size_t pos, const char *what);
    const Local *findLocal(const std::string &name) const;
    void enterScope();
    void leaveScope();

    int enterBlock();
    void leaveBlock();
    int currentBlock() const { return blockStack_.empty() ? 0 : blockStack_.back(); }
    const GlobalSym *findGlobal(const std::string &name) const;
    GlobalSym *findGlobalToUpdate(const std::string &name);

    const Type *composite(const Type *a, const Type *b);
    void declareFunction(const std::string &name, const Type *returns,
                         const std::vector<const Type *> &params,
                         bool variadic, bool defining, std::size_t pos,
                         bool internal = false);
    ExprPtr defaultPromote(ExprPtr e);
    const Signature &lookupFunction(const std::string &name, std::size_t pos) const;
    const Signature *findFunction(const std::string &name) const;
    const std::vector<std::size_t> *overloadsOf(const std::string &name) const;
    const Signature &lookupSignature(const std::string &name,
                                     const std::vector<const Type *> &params,
                                     bool variadic, std::size_t pos) const;

    static std::string describeSignature(const Signature &f);
    const Type *decayedType(const Type *t);
    Rank rankArgument(const Expr &arg, const Type *param);
    // `object` is the type the call is made on for a member function and null for a free
    // one: **the implicit object parameter is ranked like any other**, which is what
    // tells `get()` from `get() const`. **By value** - `functions_` can move under you.
    Signature resolveOverload(const std::string &name,
                                     const std::vector<ExprPtr> &args,
                                     std::size_t pos,
                                     const Type *object = nullptr);

    ExprPtr newExpression(std::size_t pos);
    ExprPtr deleteExpression(std::size_t pos);
    // `try { ... } catch (T e) { ... }` - rung 6.3.
    StmtPtr tryStatement(std::size_t pos);
    // Set while a try's body is read, so a try inside one is refused.
    bool inTryBody_ = false;
    // Inside a Microsoft `catch` body, which is compiled as a funclet - a function of its own.
    bool inMsHandler_ = false;
    // Inside a handler's own block, on any target.
    bool inHandlerBody_ = false;
    // **How many Itanium handlers the statement being parsed is inside**, and
    // the loop and switch depth each of them was entered at.
    int handlerDepth_ = 0;
    std::vector<int> handlerLoopDepth_;
    std::vector<int> handlerSwitchDepth_;
    // **The pad that ends the catch this statement is inside**, for the way
    // out no jump can be written for: an exception.
    std::string handlerResumeLabel_;
    int handlerResumePtr_ = 0;
    int handlerResumeSel_ = 0;
    // Where an exception leaving this point goes.
    std::string unwindTarget(int *ptrSlot, int *selSlot) const;
    // One temporary of a block, released under its guard by a pad.
    void releaseGuarded(std::vector<StmtPtr> &steps, const Temporary &t);
    // Where each open handler's block begins, telling a `goto` from a jump.
    std::vector<std::size_t> handlerFrom_;
    // The `__cxa_end_catch` calls a jump out of `count` handlers has to make, appended after whatever destructors that jump already runs.
    void endCatches(std::vector<StmtPtr> &into, int count);
    // How many handlers a `break` or a `continue` here leaves.
    int handlersLeftByBreak() const;
    int handlersLeftByContinue() const;
    // Set when this function has a landing pad, so its prologue names the personality routine and the table.
    bool functionHasPads_ = false;
    // **The selector is an index into the whole function's type table, not into one try's handler list.**
    std::vector<std::string> functionTypes_;
    // **Set while a `try`'s body is parsed, empty everywhere else.**
    std::string tryChainLabel_;
    int tryChainPointerSlot_ = 0;
    int tryChainSelectorSlot_ = 0;
    // How much was alive when the `try` was entered. A segment's pad destroys
    // everything the *body* built, not only what its own block did: it jumps to
    // the chain instead of resuming, so no enclosing pad runs after it.
    std::size_t tryChainAliveFrom_ = 0;
    std::vector<Try *> tryBodySegments_;
    // 1-based, first occurrence winning, matching Walker::lsdaTable byte for byte.
    int typeIndexFor(const std::string &symbol);
    // Set where a function writes a `try`.
    bool functionHasTry_ = false;
    // `throw x;` - rung 6.2. Answers the statement it lowers to.
    StmtPtr throwStatement(ExprPtr value, std::size_t pos);
    StmtPtr microsoftThrow(ExprPtr value, std::size_t pos);
    // A call to something in the runtime, named by its symbol and needing no
    // declaration - the same shape callAllocator has used for operator new.
    // A temporary built by a named constructor from arguments already parsed.
    ExprPtr constructTemporary(const Type *plain, const Signature &ctor,
                               std::vector<ExprPtr> args, bool zeroFirst,
                               std::size_t pos);
    // **[over.ics.user]**.
    const Signature *convertingConstructor(const Type *to, const Expr &from);
    // The conversion function on `from` giving `to`, or - with `to` null - any
    // scalar, `bool` first. The mirror of the constructor above.
    const Signature *conversionFunction(const Type *from, const Type *to,
                                        bool allowExplicit = false);
    // A class where a number or a pointer is wanted, converted by its own.
    ExprPtr contextualScalar(ExprPtr e, std::size_t pos, const char *what);
    // The single conversion to a scalar, or null where there are none or two.
    const Signature *soleNumericConversion(const Type *from);
    // **Only one user-defined conversion may appear in a sequence**.
    int rankingConversion_ = 0;
    // The conversion itself, or null when none is needed or possible.
    ExprPtr userConversion(const Type *param, ExprPtr &arg, std::size_t pos);
    // **[over.ics.user] through a conversion *function***: one argument of
    // another class that converts to `cls`. An explicit one counts only in
    // direct-initialisation, [over.match.copy]/1.
    bool convertThroughConversionFunction(std::vector<ExprPtr> &args,
                                          const Type *cls, bool directInit,
                                          std::size_t pos);
    bool constructorViable(const Type *cls, const std::vector<ExprPtr> &args);
    void refuseUnrelatedClassCast(const Expr &v, const Type *to,
                                  std::size_t pos, const char *what);
    // **Copy-initialise a class into a slot somebody else owns**, as one
    // expression. `constructTemporary` and `materialiseCopy` each allocate a
    // slot of their own, and both arms of a `?:` have to build into one.
    ExprPtr buildInto(const Type *cls, int slot, ExprPtr value, int guard,
                      std::size_t pos);
    // Every temporary an arm made gets a guard, set at the end of that arm.
    ExprPtr markArmTemporaries(ExprPtr arm, std::size_t from, std::size_t to);
    // The same, for an operand `&&` or `||` may skip; the operand's value is
    // kept and handed back, since the comparison is what reads it.
    ExprPtr markSkippableTemporaries(ExprPtr operand, std::size_t from,
                                     std::size_t to);
    ExprPtr runtimeCall(const char *symbol, const Type *returns,
                        std::vector<ExprPtr> args);
    // _Unwind_Resume(exception), or TI's __cxa_end_cleanup(), which takes nothing.
    ExprPtr resumeCall(std::vector<ExprPtr> args);
    ExprPtr callAllocator(const char *itanium, const char *microsoft,
                          const Type *returns, ExprPtr arg, std::size_t pos);
    int newTemps_ = 0;

    void parseArguments(std::vector<ExprPtr> &args);
    // The copy the caller makes for a by-value class argument: a temporary in the
    // caller's frame, built by the copy constructor, whose address the callee receives.
    // And `p != 0 ? (body, 1) : 0`, which [expr.delete]/2 wraps a destructor call in.
    ExprPtr guardAgainstNull(const std::string &temp, int slot,
                             const Type *ptr, ExprPtr body);
    ExprPtr materialiseCopy(const Type *type, ExprPtr arg, std::size_t pos,
                            const std::string &what,
                            std::vector<Temporary> &destroy);
    std::vector<Temporary> pendingTemps_;
    // The temporaries each statement of the block being parsed made, so that the cleanup regions can name them.
    std::vector<Temporary> statementTemps_;
    // **Where a function body's cleanup regions start**, which on Windows is
    // before its by-value parameters: the callee destroys those there, and an
    // exception leaving the body has to reach them.
    std::size_t bodyCleanupFrom_ = 0;
    int guardFlag();
    // **Every guard starts clear, and the one place that is true of every path
    // is the function's entry.**
    std::vector<int> guardSlots_;
    ExprPtr setGuard(int flag, int value);
    // **Taking over a call's result slot takes over its destruction too.**
    void claimCallResult(Call &c, int slot);
    ExprPtr endFullExpression(ExprPtr e);
    // Drop the temporary this expression yields from the pending list, because its ownership is leaving the frame.
    bool releaseTemporary(const Expr &value);
    // [class.temporary]/5 - a temporary bound to a reference outlives its full
    // expression and is destroyed with the reference's scope instead.
    bool extendTemporary(const Expr &addr, const std::string &name);
    void flushTemporaries(std::vector<StmtPtr> &into);
    ExprPtr completeCall(const std::string &name, const std::string &symbol,
                         ExprPtr callee, const Type *returns,
                         const std::vector<const Type *> &params, bool variadic,
                         std::size_t pos, std::vector<ExprPtr> args,
                        bool hasThis = false, int vbInit = 1);

    void parameterTypes(std::vector<const Type *> &params, bool &variadic);
    // **Microsoft mangles a parameter from before [dcl.fct]/5 drops its cv.**
    const Type *parameterAsWritten(const Type *declared, const Type *adjusted);
    // `fn` respelled for the Microsoft mangler where this function's
    // parameters were written differently from their types, and `fn` itself
    // otherwise.
    const Type *manglingType(const Type *fn);
    std::vector<const Type *> writtenParams_;
    std::vector<const Type *> writtenFor_;

    // **A default argument is kept as a place in the token stream, not as a parsed
    // expression**: [dcl.fct.default] evaluates it afresh at every call that leaves it
    // out. `pendingDefaults_` carries them to declare(); `defaultArgs_` keys them.
    std::vector<std::size_t> pendingDefaults_;
    std::map<std::string, std::vector<std::size_t> > defaultArgs_;
    // **[dcl.fct.default]/5 reads a default in the scope of its declaration.**
    std::map<std::string, std::vector<std::string> > defaultArgNamespace_;
    // Past one default argument.
    void skipDefaultArgument();
    // **A member's own initialiser is kept as a place in the token stream**, the same way a default argument is and for the same reason.
    std::vector<std::string> namespaceStack_;
    // Namespaces named by a `using namespace` still open here, innermost last.
    std::vector<std::string> usingNamespaces_;
    // **A using-declaration is an alias, not a table.**
    std::map<std::string, std::string> usingDeclarations_;
    // **An unnamed namespace is a named one that nothing outside can name.**
    bool inUnnamedNamespace_ = false;
    // **The body being read belongs to a static member.**
    bool inStaticMember_ = false;
    // Whether a declaration here has internal linkage: written `static`, or
    // inside an unnamed namespace, which says the same thing about a name.
    bool internalLinkage(StorageClass sc) const {
        return sc == StorageStatic || inUnnamedNamespace_;
    }
    // Every namespace ever opened, by full path. A qualified name has to be
    // told from a class's - `N::f` and `S::f` are written the same and mean
    // different lookups - and this is what tells them apart.
    std::set<std::string> namespaces_;
    std::string namespacePrefix() const {
        std::string out;
        for (std::size_t i = 0; i < namespaceStack_.size(); i++)
            out += namespaceStack_[i] + "::";
        return out;
    }
    // The qualified name to look a written one up under: the enclosing namespaces from
    // the innermost outwards, then whatever a `using namespace` has opened, then the
    // name itself. `exists` says which table to ask, functions and variables differing.
    std::string qualifyForLookup(const std::string &name,
                                 bool (Parser::*exists)(const std::string &) const) const;
    // The name a using-declaration redirects `key` to, following a chain of them
    // and stopping at a fixed depth so that `using A::x;` and `using B::x;`
    // written into a cycle cannot hang the parser instead of failing a lookup.
    std::string followUsingDeclaration(const std::string &key) const;
    bool hasFunctionNamed(const std::string &key) const;
    bool hasGlobalNamed(const std::string &key) const;
    bool hasTypeNamed(const std::string &key) const;
    bool hasEnumNamed(const std::string &key) const;
    // An enumerator named through the class it was written in, and through that class's bases.
    const EnumConst *enumInClass(const Type *cls, const std::string &name) const;
    // findEnum's class half on its own: an enumerator of the class being
    // parsed is at *class* scope and beats a global, where one at namespace
    // scope does not beat a member.
    const EnumConst *findClassEnum(const std::string &name) const;
    std::size_t qualifiedTypeEnd() const;
    std::vector<std::string> lookupKeys(const std::string &name,
                                        const Type *left,
                                        const Type *right) const;

    std::map<std::string, std::size_t> memberInit_;
    // Whether this class wrote a member initialiser - [dcl.init.aggr]/1.
    bool hasMemberInitialiser(const std::string &tag) const;
    // [class.ctor]/5: default-initialising a class whose implicit default
    // constructor is deleted - a const or reference member with no initialiser.
    void refuseDeletedDefaultInit(const Type *t, const std::string &name,
                                  std::size_t pos);
    // To the ',' or ';' that ends one, counting brackets.
    void skipMemberInitialiser();
    // The statements a constructor of this class needs for the members it did
    // not initialise itself - [class.base.init]/9, where an initialiser the
    // class wrote stands in for a mem-initialiser nobody wrote.
    std::vector<StmtPtr> memberInitialisers(const std::string &tag,
                                            const Type *type, int thisSlot,
                                            const std::set<std::string> &already,
                                            std::size_t pos);
    // The same for one member: the store its own initialiser asks for, or
    // nullptr where the class wrote none.
    StmtPtr memberInitialiser(const std::string &tag, const Type *type,
                              const Member &m, int thisSlot, std::size_t pos);
    // The constructor call for a class-typed member - [class.base.init]/8 with `args`
    // empty, the mem-initialiser `: m(args)` otherwise. Answers nullptr for a member
    // whose type has no constructors, leaving `args` untouched for the scalar path.
    StmtPtr constructMember(const std::string &cls, const Type *type,
                            const Member &m, int thisSlot,
                            std::vector<ExprPtr> &args, std::size_t pos,
                            bool implicit);
    // [dcl.fct.default]/4: the defaults have to be a suffix of the parameter list. Asked
    // by both parameter-list parsers - declarations and definitions - because a
    // definition may carry the defaults where the declaration did not.
    void requireDefaultsAreASuffix(const std::vector<std::size_t> &defaults,
                                   std::size_t pos);
    // The lowest number of arguments a call may give this function.
    std::size_t leastArguments(const Signature &f) const;
    // Fill in what the call left out, by re-reading each default expression. By value for
    // the same reason, and this is the site that does the parsing: re-reading tokens is
    // exactly what can grow `functions_` under a caller still holding a reference.
    void applyDefaults(Signature f, std::vector<ExprPtr> &args,
                       std::size_t pos);
    void blockFunctionDeclaration(const Declared &d);

    ExprPtr finishCall(const std::string &name, const std::string &symbol,
                       ExprPtr callee, const Type *returns,
                       const std::vector<const Type *> &params, bool variadic,
                       std::size_t pos);

    // How deep inside 'extern "C"' the parser currently is. Zero means C++
    // linkage, which is what a name has unless someone says otherwise.
    int cLinkage_ = 0;
    bool linkageSpecification();
    std::string functionSymbol(const std::string &name, const Type *returns,
                               const std::vector<const Type *> &params,
                               bool variadic, bool internal, std::size_t pos);
    std::string dataSymbol(const std::string &name, const Type *type,
                           bool isStatic, std::size_t pos);

    ExprPtr objectRef(const std::string &name);
    // The two halves of it, because class scope sits between them.
    ExprPtr localRef(const std::string &name);
    ExprPtr globalRef(const std::string &name);
    ExprPtr useReference(ExprPtr e);
    ExprPtr bindReference(const Type *ref, ExprPtr init, std::size_t pos,
                          const std::string &what);
    int refTemps_ = 0;

    struct Init {
        bool isList = false;
        ExprPtr value;
        std::vector<Init> items;
        std::size_t pos = 0;
    };

    struct InitStep {
        const Member *member = nullptr;
        long long index = 0;
    };

    // **[dcl.init]/8: `T()` on a class with no user-provided constructor is
    // value-initialisation, which zeroes it.** `initZero` says the same in statements;
    // these two say it as an expression, rooted at a frame slot and not at a name.
    ExprPtr pathAccess(ExprPtr root, const std::vector<InitStep> &path);
    void zeroLeaves(const Expr &root, const Type *type,
                    std::vector<InitStep> &path, std::vector<ExprPtr> &out);
    ExprPtr zeroChain(const Expr &root, const Type *type);
    ExprPtr functionalCast(const Type *to, std::size_t pos);
    // `T{}` in an expression: the value-initialisation `T()` already gives.
    ExprPtr bracedValueInit(const Type *to, std::size_t pos);
    // Every value carried as a double, exactly, or the fold says so.
    bool foldDouble(const Expr &e, const Target &target, long double *out,
                    bool *past53, bool *x87Rounded) const;
    const Type *simpleTypeKeyword() const;

    struct InitCursor {
        std::vector<Init> *items = nullptr;
        std::size_t at = 0;
        bool done() const { return items == nullptr || at >= items->size(); }
        Init &cur() const { return (*items)[at]; }
    };

    // **[dcl.spec.auto]/7: every declarator of one `auto` declaration deduces
    // the same type.**
    void checkOneDeducedType(const Type *&first, const Type *now,
                             const std::string &name, std::size_t pos);
    // What the last `auto` deduced *for itself* - `auto *p = &i` leaves `int`
    // here and returns `int *`. Written by deduceAutoFrom and read by the
    // declaration loops, which compare declarators of one declaration.
    const Type *lastDeducedAuto_ = nullptr;
    Init parseInitialiser();
    // The initialiser inside the parentheses of `int z(5);` - one expression, [dcl.init]/16.
    Init parenthesisedInitialiser(const Declared &d);
    // **Is the `(` this sits on an initialiser rather than a parameter list?**
    bool atParenInitialiser();
    // Answers whether a declaration is initialised by braces, and refuses the
    // braces this compiler does not read - which is every pair with a value in
    // it. The empty pair is value-initialisation and parseInitialiser takes it.
    bool atBracedInitialiser(const std::string &name);
    bool constantInitialiser(const Type *t, const Init &in, long long *out) const;
    bool constantFloatingInitialiser(const Type *t, const Init &in,
                                     long double *out) const;
    bool foldFloating(const Expr &e, long double *out) const;

    // **A `constexpr` function, kept so that fold() can run it.** [dcl.constexpr] in
    // C++11 lets the body be one return statement, which makes evaluating a call an
    // expression fold. The function is still compiled and callable at run time.
    struct ConstexprFn {
        const Expr *value = nullptr;   // owned by the Function in the Program
        std::vector<int> slots;        // parameter frame slots, in order
        std::size_t pos = 0;
    };
    std::map<std::string, ConstexprFn> constexprFns_;   // by mangled symbol

    // One frame per call being folded, holding what each parameter slot is worth.
    mutable std::vector<std::vector<std::pair<int, long long> > > constexprFrames_;

    const Expr *singleReturnValue(const Stmt &body) const;
    ExprPtr targetFor(const std::string &name, const std::vector<InitStep> &path);

    void initStore(const std::string &name, std::vector<InitStep> &path,
                   ExprPtr value, std::size_t pos, std::vector<StmtPtr> &out);
    void initZero(const std::string &name, std::vector<InitStep> &path,
                  const Type *type, std::size_t pos, std::vector<StmtPtr> &out);
    void emitString(const std::string &name, std::vector<InitStep> &path,
                    const Type *type, const StrLit *s, std::size_t pos,
                    std::vector<StmtPtr> &out);
    void emitFill(const std::string &name, std::vector<InitStep> &path,
                  const Type *type, InitCursor &c, std::vector<StmtPtr> &out);
    void emitAggregate(const std::string &name, std::vector<InitStep> &path,
                       const Type *type, InitCursor &c, std::size_t pos,
                       std::vector<StmtPtr> &out);
    // **List-initialisation of a class with an initializer_list constructor.**
    const Signature *initializerListConstructor(const Type *cls,
                                                const Type **ilType);
    ExprPtr buildInitializerList(const Type *ilType, Init &in, std::size_t pos,
                                 std::vector<StmtPtr> &into);
    int ilTemps_ = 0;

    void emitInit(const std::string &name, std::vector<InitStep> &path,
                  const Type *type, Init &in, std::vector<StmtPtr> &out);
    // [dcl.init.list]/7: a value inside braces may not narrow. Asked of every
    // scalar a braced list reaches, for a local and at file scope alike.
    void checkNarrowing(const Type *to, const Expr &value, std::size_t pos,
                        const std::string &what);

    void flattenScalar(const Type *type, Init &in, int base,
                       std::vector<GlobalPiece> &out);
    void flattenFill(const Type *type, InitCursor &c, int base,
                     std::vector<GlobalPiece> &out);
    void flattenAggregate(const Type *type, InitCursor &c, int base,
                          std::vector<GlobalPiece> &out);
    void flattenInit(const Type *type, Init &in, int base,
                     std::vector<GlobalPiece> &out);

    void skipInit(const Type *type, InitCursor &c);
    long long inferredLength(const Init &in, const Type *element, std::size_t pos);
    static const StrLit *stringInitialiser(const Init &in, const Type *type);

    void topLevel(Program &program);
    StmtPtr block();
    StmtPtr statement();
    StmtPtr statementBody();
    StmtPtr declarationBody();
    StmtPtr forStatement();
    StmtPtr switchStatement();
    StmtPtr caseLabel();
    StmtPtr gotoLabel();
    StmtPtr declaration();
    // **A declaration in a condition** - [stmt.select]/2 and [stmt.iter]/2.
    bool conditionDecl_ = false;
    std::string conditionName_;
    bool atConditionDeclaration() const;
    ExprPtr ifConditionDeclaration(std::vector<StmtPtr> &setup);
    ExprPtr whileConditionDeclaration();
    void resolveGotos();

    bool staticAssertion();
    // `using X = T;` is an alias declaration and not a using-declaration.
    void refuseAliasDeclaration();
    // `= default` and `= delete` sit where `= 0` does, and a constructor
    // reaches that position by a different door than a member function.
    void refuseDefaultedOrDeleted();
    bool exceptionSpecification();
    // Set by exceptionSpecification() at each place a parameter list can be closed, read and cleared by whichever declare* call follows.
    bool pendingNoexcept_ = false;
    // **Is the function being parsed declared `noexcept`?** [except.spec]/9.
    bool inNoexceptFunction_ = false;
    // Set when `explicit operator T()` is seen, consumed by the next declareFunction of a conversion name, which stamps the signature.
    bool pendingExplicitConversion_ = false;
    // How many potentially-throwing things the expression being parsed has
    // reached. `noexcept(e)` reads it; nothing else does.
    int mayThrow_ = 0;
    long long constantExpression(const char *what);
    bool fold(const Expr &e, long long *out, std::size_t pos) const;

    void typedefFunctionSuffix(Declared &td);

    bool foldAddress(const Expr &e, std::string *sym, long long *off) const;
    bool addressOfObject(const Expr &e, std::string *sym, long long *off) const;
    long long narrowTo(long long v, const Type *t) const;

    ExprPtr expr();
    ExprPtr assign();
    ExprPtr conditional();
    ExprPtr bitOr();
    ExprPtr bitXor();
    ExprPtr bitAnd();
    ExprPtr compound(BinOp op, ExprPtr target, ExprPtr value, std::size_t pos);
    ExprPtr incDec(ExprPtr target, bool increment, bool prefix, std::size_t pos);
    ExprPtr clonePure(const Expr &e);
    ExprPtr cloneLvalue(const Expr &e, std::size_t pos);
    ExprPtr shiftOf(BinOp op, ExprPtr lhs, ExprPtr rhs, std::size_t pos);
    ExprPtr logicalOr();
    ExprPtr logicalAnd();
    ExprPtr equality();
    ExprPtr relational();
    ExprPtr shift();
    ExprPtr add();
    ExprPtr mul();
    ExprPtr castExpr();
    // `a .* p` and `p ->* q` - [expr.mptr.oper], which sits between a cast and
    // a multiplication. What it builds is the object's address plus the
    // offset the member pointer holds, read as the member's type.
    ExprPtr memberPointerExpr();
    ExprPtr applyMemberPointer(ExprPtr addr, ExprPtr mp, std::size_t pos,
                               bool constObject);
    // `&S::f` - the ABI's pair, built into a slot of this frame.
    ExprPtr boundMemberPointer(const Type *cls, const Signature &f,
                               std::size_t pos);
    // **`o.*p` for a member *function* pointer has to carry two things to the call** -
    // the object's address and the code pointer - and no expression holds a pair. The
    // address is left here and the `(` in `postfix` picks it up. One token wide.
    ExprPtr boundThis_;
    const Type *boundFn_ = nullptr;
    std::size_t boundAt_ = 0;
    ExprPtr staticCast(std::size_t pos);
    ExprPtr constCast(std::size_t pos);
    ExprPtr dynamicCast(std::size_t pos);
    ExprPtr dynamicCastToVoid(ExprPtr v, const Type *to);
    // A function named but not called, as a pointer to it; null if `key`
    // names no function. Shared by the bare and the qualified spelling.
    ExprPtr functionAsValue(const std::string &key, std::size_t pos);
    ExprPtr reinterpretCast(std::size_t pos);
    ExprPtr unary();
    ExprPtr postfix();
    ExprPtr primary(Program *program);

    ExprPtr arithmetic(BinOp op, ExprPtr lhs, ExprPtr rhs, std::size_t pos);
    ExprPtr comparison(BinOp op, ExprPtr lhs, ExprPtr rhs, std::size_t pos);
    // Null unless an operand has class type, in which case the call it built.
    // Takes its operands by reference because it leaves them alone when it
    // answers null, and the built-in path below still needs them.
    ExprPtr overloadedBinary(BinOp op, ExprPtr &lhs, ExprPtr &rhs,
                             std::size_t pos);

    // Which side of [over.match.oper] won.
    enum class OperatorChoice { None, Member, NonMember };
    // `right` is null for a unary operator, where the operand is `left` and
    // a member candidate writes no parameter at all.
    OperatorChoice resolveOperator(const std::string &name, const Expr &left,
                                   const Expr *right, std::size_t pos);
    // `-v`, `!v`, `*p`, `++v`. Null when the operand is not a class or the class has no
    // such operator, and the built-in path below is then reached unchanged - which keeps
    // `&obj` the ordinary address-of for a class that declares no `operator&`.
    ExprPtr overloadedUnary(const char *spelling, ExprPtr &operand,
                            std::size_t pos);
    const Type *ptrdiffType();
    ExprPtr pointerAdd(ExprPtr p, ExprPtr n);
    ExprPtr pointerSub(ExprPtr l, ExprPtr r, std::size_t pos);

    Program *current_ = nullptr;
};
