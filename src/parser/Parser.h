#pragma once

#include "../Source.h"
#include "../Abi.h"
#include "../Ast.h"
#include "../Lexer.h"
#include "../Mangle.h"
#include "../Type.h"

#include <functional>
#include <map>
#include <memory>
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
    // Where the reading stood when an error ended it - the statement under way, else the member, else the
    // declaration, in the program's own text - or npos(), where it stood in none.
    static std::size_t npos() { return static_cast<std::size_t>(-1); }
    std::size_t failedAt() const { return stmtAt_ != npos() ? stmtAt_ : memberAt_ != npos() ? memberAt_ : declAt_; }

    Parser(const Source &src, std::vector<Token> tokens,
           TypeTable &types, const Target &target, const Abi &abi)
        : src_(src), tokens_(std::move(tokens)), types_(types), target_(target),
          abi_(abi) {}

    Program parse();

    // **[temp.res]/8-10: a function template defined, never instantiated**
    // is made once with stand-ins (types int or double, values 1) in a parser
    // thrown away; the first failure is kept, `report` says if it is its own.
    struct ProbeResult {
        bool failed = false;
        bool report = false;
        std::size_t pos = 0;
        std::string why;
        std::size_t templateAt = 0;
    };
    void setProbe(int variant, const std::set<std::size_t> &skip) { probeVariant_ = variant; probeSkip_ = skip; }
    const ProbeResult &probeResult() const { return probeResult_; }
    std::size_t probeCandidates() const { return probeTargets().size(); }

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
        // A const local of the function around a lambda, read in the lambda's body for its value
        // and nothing else - [expr.prim.lambda]/12, a use that is no odr-use needs no capture (1.27).
        bool constantOnly = false;
        // **[class.copy]/31 elides a `return` copy, and excludes a parameter.**
        bool isParameter = false;
        bool noexceptPointer = false;   // a pointer to function declared `noexcept`
        // And a `throw` copy, excluding a catch clause's parameter too.
        bool isCatchParameter = false;
        // A by-value class parameter that arrived by address is lowered to a reference, so by its slot alone it looks like `T &t`.
        bool byValueByAddress = false;
        // **[stmt.dcl]/3: a jump may not enter this object's scope** - set for an automatic object with an initialiser, a constructor or a destructor, the three things a jump landing past its declaration would skip.
        bool guardsJump = false;
        // Declared with no initialiser, and neither read nor written yet.
        mutable bool unset = false;
        // A member of a local anonymous union: its slot is the union's, given back once, by the union.
        bool sharesSlot = false;
        // Which object this is: two locals of one function may share a frame slot once the first's scope has closed.
        int serial = 0;
        // Its position among a trailing return type's parameters, unnamed ones counted - `fp_`, `fp0_`.
        int trailingParam = -1;
        // A static or extern thread_local in a block, and whether a use goes through its wrapper.
        bool isThreadLocal = false;
        bool tlsViaWrapper = false;
    };

    // A frame slot a closed scope gave back, for a later local of exactly that stored type: `FreeSlot`, in `freeSlots_`.
    struct FreeSlot { const Type *stored; int offset; int align; };

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
        // **thread_local** - [basic.stc.thread]: whether a use reaches it through the ABI's wrapper
        // or guard (declared elsewhere, or built by __tls_init), and whether it has external linkage.
        bool isThreadLocal = false;
        bool tlsViaWrapper = false;
        bool tlsExternal = false;
    };

    // **`throw(T...)` as written**, adjusted by [except.spec]/2; `throw()` is noexcept and not one of these.
    struct DynamicSpec {
        bool written = false;
        std::vector<const Type *> types;
        std::size_t pos = 0;
        bool allowsAll = false;   // `noexcept(false)`, which is not `written`
        bool sameSet(const DynamicSpec &o) const;
    };

    struct Signature {
        std::string name;
        std::string symbol;
        const Type *returns;
        std::vector<const Type *> params;
        bool variadic;
        bool defined;
        std::size_t pos;
        // C linkage, and `main`: the symbol is the name. `main` is one function, and a name
        // has at most one with C linkage beside its C++ overloads ([dcl.link]/6).
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
        DynamicSpec spec = DynamicSpec();
        // **Nobody wrote this one.** An implicitly declared special member is put in the
        // table so overload resolution finds it, and given a body only if something
        // calls it - which is what keeps the symbol list level with the oracles.
        bool implicit = false;
        bool used = false;
        // `S::S() = default;` - [dcl.fct.def.default]/5: user-provided, and given the implicit one's body.
        bool defaultedOutside = false;
        // **A specialization is a candidate like any other, and loses a tie.**
        // [over.match.best]: where a specialization and an ordinary function are equally
        // good, the ordinary one wins. Its unbound parameter list serves [temp.func.order].
        bool fromTemplate = false;
        // A member template's specialization: ranked after an ordinary member of the same parameters.
        bool fromMemberTemplate = false;
        const Type *pattern = nullptr;
        // Defined inside its class, so [dcl.inline]/6 makes it inline and never the key function.
        bool inlineBody = false;
        // `= delete` - [dcl.fct.def.delete]: ranked like any other candidate, and refused where chosen.
        bool deleted = false;
        // **The ref-qualifier, half of a member's identity beside its const** - [over.load]/2.
        RefQual refThis = RefQual::None;
        // `f() volatile`, the other half of the cv on `this`.
        bool volatileThis = false;
        // [namespace.memdef]/3: friend-declared first, found by ADL only
        bool hiddenFriend = false;
    };
    // The cv of a member function's `this` as its function type carries it, Type::kThisConst and kThisVolatile.
    static unsigned thisCvOf(const Signature &f) {
        return (f.constThis ? Type::kThisConst : 0u) | (f.volatileThis ? Type::kThisVolatile : 0u);
    }

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
        bool isFinal = false;              // [class.virtual]/4: no later override may take it
        RefQual refThis = RefQual::None;   // `f() &` and `f() &&` take two slots
        bool volatileThis = false;         // and `f()` and `f() volatile` two
    };
    // `override` and `final` as written on one declaration, each with where it was written.
    // A constructor rather than member initialisers: g++ refuses those in a default argument of the enclosing class.
    struct VirtSpecifiers {
        bool isOverride, isFinal;
        std::size_t overrideAt, finalAt;
        VirtSpecifiers() : isOverride(false), isFinal(false), overrideAt(0), finalAt(0) {}
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
                          bool constThis, RefQual refThis, bool volatileThis = false);
    // `volatile` after a member function's parameter list, read with its `const` and taken where its
    // Signature is made, as pendingNoexcept_ is.
    bool pendingVolatileThis_ = false;
    // **A Microsoft class holds a vfptr and a table per polymorphic base**:
    // where it sits, the bases naming it (clang's MangledPath), each slot's
    // entry and `this`; the first is the class's own, vtables_[tag].
    struct MsVftable {
        int at = 0;
        std::vector<const Type *> path;
        const Type *nextToMangle = nullptr;
        std::vector<VSlot> slots;
        std::vector<int> thisAt;
        // A table inside a virtual base: reached through the vbtable, `atInVbase` past the base.
        const Type *vbase = nullptr;
        int atInVbase = 0;
        // That base has a vtordisp in front of it here, and these entries are overridden below it: each
        // goes through a vtordisp thunk.
        bool vtordisp = false;
        std::vector<char> below;
    };
    // The virtual bases a class puts a vtordisp in front of (cl's /vd1).
    std::map<std::string, std::set<const Type *> > msVtordisp_;
    std::vector<MsVftable> msVftables(const Type *cls);
    // **The final overrider of a virtual base's slot** below `k`, which sits at `kAt` in `root`: `k`'s own,
    // else the first found along its bases, never in `vbase` or under it; `this` as it wants it in `root`.
    bool msFinalOverrider(const Type *root, const Type *k, int kAt, const Type *vbase, const VSlot &slot,
                          std::string *symbol, int *thisAt, bool *pure);
    // An override of a virtual base's slot is entered at that base, which is laid only after the class's
    // members are read: the base and where in it the table is, resolved to msThisAdjust_ then (M3).
    struct MsVbaseAdjust { std::string cls; const Type *vbase; int within; };
    std::map<std::string, MsVbaseAdjust> msVbaseAdjust_;
    // `(T *)((char *)self + by)`, `self` itself where `by` is 0.
    ExprPtr msShifted(ExprPtr self, int by);
    // The address of a class's first vfptr, which the runtime's RTTI calls take: the object where the
    // vfptr leads it, else its virtual base's through the vbtable, as cl passes it (M3). `p` is a `cls *`.
    ExprPtr msToVfptr(ExprPtr p, const Type *cls);
    bool msVfptrInVirtualBase(const Type *cls);
    // A body entered at a virtual base's vfptr moves `this` back to its class first.
    void msEnterAdjusted(std::vector<StmtPtr> &body, int thisSlot, const Type *self, const std::string &symbol);
    // Whether `d` has `b` among its bases at any depth.
    static bool msHasBase(const Type *d, const Type *b);
    // The rows of a class's _RTTIBaseClassArray with its virtual bases in them, read through the
    // complete class's vbtable; false where a non-virtual base is repeated.
    bool msRttiRows(const Type *cls, std::vector<MicrosoftBaseClass> *rows, int *attributes);
    // Where a virtual function's `this` points in its class, where that is not the class itself: the
    // vfptr of the first table holding the slot it overrides, as cl's "this adjustor" (X::b: 16).
    std::map<std::string, int> msThisAdjust_;
    int msThisAdjust(const std::string &symbol) const;
    // What an adjustor thunk's name is built from: the function's declaration as it was mangled.
    struct MsVirtualDecl { std::string cls, name; const Type *fn; bool constThis; RefQual refThis; };
    std::map<std::string, MsVirtualDecl> msVirtualDecls_;
    std::set<std::string> msPure_;          // declared `= 0`, whichever table holds the slot
    std::string msAdjustorThunk(const std::string &cls, const Type *type, const VSlot &slot,
                                int adjust, std::size_t pos, bool vtordisp = false);
    // **The Itanium vtable group and its address points**: for each base
    // subobject with a table in it, by (class, offset in the object), how
    // far into the symbol that table's address point sits.
    struct VtableGroup {
        std::vector<GlobalPiece> pieces;
        std::map<std::pair<const Type *, int>, int> points;
    };
    // Every group written, by symbol: a `_ZTV`, or a construction `_ZTC`.
    std::map<std::string, VtableGroup> groups_;
    // **The VTT of a class with virtual bases**, by tag: where its own
    // constructor finds the sub-VTT for a base, the secondary virtual
    // pointer for a subobject, and the virtual VTT for a virtual base.
    struct VttLayout {
        std::vector<GlobalPiece> pieces;
        std::map<const Type *, int> subVtt;
        std::map<std::pair<const Type *, int>, int> secondary;
        std::map<const Type *, int> virtualVtt;
    };
    std::map<std::string, VttLayout> vtts_;
    // The hidden VTT parameter of the C2 or D2 being emitted, -1 outside one.
    int vttSlot_ = -1;
    const Type *vttType() const {
        return types_.pointerTo(types_.pointerTo(types_.get(Kind::Void)));
    }
    // One subobject with a vptr, as the constructor's walk finds it.
    struct VptrSite {
        const Type *cls;
        int offset;             // in the complete object being laid out
        bool morallyVirtual;    // reached through a virtual base
        const Type *nearestVBase;
        int fromVBase;          // its distance from that virtual base
    };
    void vptrSites(const Type *cls, int at, bool morally, const Type *vbase,
                   int fromVBase, const Type *layout,
                   std::set<const Type *> &seen,
                   std::vector<VptrSite> &out) const;
    // The virtual bases of a class in the order their `vbase_offset`
    // entries are built, each with its offset in the layout class.
    void vbaseComponents(const Type *cls, const Type *layout, int clsAt,
                         std::set<const Type *> &seen,
                         std::vector<std::pair<const Type *, int> > &out)
        const;
    static int virtualBaseAt(const Type *layout, const Type *vbase);
    // The final overrider of a base's slot in `root`, and where it sits.
    struct Overrider { const Type *cls; int at; std::string symbol; bool pure; };
    // One `vcall_offset` in a virtual base's table, and whose function it is for.
    struct VcallEntry {
        const Type *cls;
        std::string name;
        std::vector<const Type *> params;
        bool constThis;
        RefQual refThis;
        long long value;
    };
    void vcallEntries(const Type *y, int yAt, const Type *vbase, int vbaseAt,
                      const Type *root, int rootAt, const Type *layout,
                      std::vector<VcallEntry> &out, std::size_t pos);
    const VSlot *declaredSlot(const Type *cls, const VSlot &s) const;
    Overrider finalOverrider(const Type *root, int rootAt, const VSlot &s,
                             const Type *target, int targetAt,
                             const Type *layout, std::size_t pos);
    std::string synthesizeVirtualThunk(const Type *type, const VSlot &slot,
                                       long long fixed, long long vcallBack,
                                       std::size_t pos);
    // The class whose subobjects are being enumerated, with a fresh visited
    // set per walk: a virtual base is one subobject however it is reached.
    struct Subobject { const Type *cls; int at; };
    void subobjectsOf(const Type *x, int xAt, const Type *layout,
                      std::set<const Type *> &seen,
                      std::vector<Subobject> &out) const;
    bool containsSubobject(const Type *y, int yAt, const Type *x, int xAt,
                           const Type *layout) const;
    void layoutOneVtable(VtableGroup &g, const Type *x, int xAt,
                         bool xIsVirtual, const Type *vbase, int vbaseAt,
                         const Type *root, int rootAt, const Type *layout,
                         const std::string &typeInfo, std::size_t pos);
    void layoutSecondaryVtables(VtableGroup &g, const Type *x, int xAt,
                                const Type *vbase, int vbaseAt,
                                const Type *root, int rootAt,
                                const Type *layout, bool construction,
                                const std::string &typeInfo, std::size_t pos);
    void layoutVirtualBaseVtables(VtableGroup &g, const Type *x,
                                  const Type *root, int rootAt,
                                  const Type *layout, bool construction,
                                  const std::string &typeInfo,
                                  std::set<const Type *> &seen,
                                  std::size_t pos);
    VtableGroup buildVtableGroup(const Type *cls, int clsAt,
                                 bool clsIsVirtual, const Type *layout,
                                 const std::string &typeInfo,
                                 std::size_t pos);
    // A group written out under `symbol`, its address points recorded.
    void emitVtableGroup(const std::string &symbol, VtableGroup g);
    std::string constructionVtable(const Type *base, int at,
                                   bool baseIsVirtual, const Type *layout,
                                   const std::string &typeInfo,
                                   std::size_t pos);
    void emitItaniumVtables(const Type *cls, const std::string &tag,
                            const std::string &symbol, std::size_t pos);
    void vttPart(const Type *cls, int clsAt, const std::string &group,
                 const Type *layout, bool top, const std::string &typeInfo,
                 VttLayout &out, std::size_t pos);
    void vttSecondaryPointers(const Type *x, int xAt, bool morally,
                              const std::string &group, const Type *layout,
                              bool top, std::set<const Type *> &seen,
                              VttLayout &out, int from, std::size_t pos);
    void vttVirtualParts(const Type *x, const Type *layout,
                         const std::string &typeInfo,
                         std::set<const Type *> &seen, VttLayout &out,
                         std::size_t pos);
    // The class's mangled name alone - what `_ZTV`, `_ZTT` and `_ZTC` share.
    std::string itaniumClassEncoding(const Type *cls) const;
    // The VTT argument a call to a C2 or D2 carries: the sub-VTT for a
    // non-virtual base from this function's own VTT, or an entry of the
    // class's `_ZTT` for the complete object's calls.
    ExprPtr vttForBase(const Type *cls, const Type *base);
    ExprPtr vttOfClass(const Type *cls, int entry);
    bool takesVtt(const Type *cls) const {
        return !target_.microsoftNames() && cls != nullptr &&
               cls->hasVirtualBase();
    }
    // `*(vtt + n)`, the address point an entry holds.
    ExprPtr vttEntry(ExprPtr vtt, int entry);
    // The offset to a virtual base read through the object's vptr.
    ExprPtr vbaseOffsetRead(ExprPtr objectChars, const Type *cls,
                            const Type *vbase);
    // The vptrs a constructor or destructor of `cls` stores, the whole
    // object's - its own, every non-primary base's at any depth, every
    // virtual base's - and where each comes from.
    std::vector<StmtPtr> itaniumVptrStores(const std::string &cls,
                                           const Type *memberOf, int thisSlot);

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
        // **A friend defined inside the class is not a member of it**, and belongs to the namespace
        // the class was in, wherever the specialization was asked for.
        bool freeFunction = false;
        std::vector<std::string> friendNamespace;
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
    // Set while a friend declaration, or a friend's body written in its class, declares its function.
    bool friendDeclaration_ = false;
    // **[dcl.inline]/6: a member defined inside its class is implicitly
    // inline**, and so is every member of a template specialization - which is
    // to say, everything that reaches `topLevel` through a *replay*.
    bool replayingInline_ = false;
    void skipBracedBlock();

    // ---- Rung 5.1: the template table, and nothing instantiated ----
    // **A pack's members as bound**: the types, and for a non-type pack the values beside them,
    // the types then being the element type once per value. One per template parameter.
    struct PackMembers : std::vector<const Type *> {
        std::vector<long long> values;
        PackMembers() {}
        PackMembers(const std::vector<const Type *> &t) : std::vector<const Type *>(t) {}
    };
    typedef std::vector<PackMembers> Packs;
    // One template parameter as written: `class T` / `typename T`, or a non-type one
    // such as `int N`. Nothing is bound to either yet.
    struct TemplateParam {
        std::string name;
        const Type *type = nullptr;   // null for a type parameter
        // `T v` after `class T`: the index of T, and `type` is T standing for itself. -1 otherwise.
        int typeOf = -1;
        // `T *P`, `const T &R`: a type built on an earlier parameter, `type` holding it standing for itself.
        bool builtOnParam = false;
        std::size_t pos = 0;
        // Where its name is, or would be: what clang points at for a template template argument of the wrong kind.
        std::size_t namePos = 0;
        // `class...`
        bool isPack = false;
        // **`template <class> class C`** - [temp.param]/1: a parameter standing for a template, with
        // its own list in `inner`, whose names nothing can refer to. `type` is null, as a type parameter's.
        bool isTemplate = false;
        std::vector<TemplateParam> inner;
        // A default argument, kept as a half-open token range [begin, end) the way a default
        // *function* argument is - instantiation replays tokens, so a default is replayed too, with
        // the earlier parameters bound. begin == 0 means there is none.
        std::size_t defBegin = 0;
        std::size_t defEnd = 0;
        // A default taken from an earlier declaration names the parameters before it as that one did -
        // `class Tr = char_traits<C>` in <iosfwd> for <iterator>'s `template <class CharT, class Traits>`.
        std::vector<std::string> defNames;
        // Where a non-type parameter's type is written, read again where it depends on earlier parameters.
        std::size_t typeAt = 0;
        // And the namespace it is read in: `Hook T::*M` of ctr's template, from main, found no `Hook` (1.82).
        std::vector<std::string> typeNamespace;
    };
    // A non-type parameter's type under a binding: T's binding for `T v`, `T *` with T's binding for `T *P`,
    // the written type otherwise.
    const Type *paramTypeIn(const TemplateParam &p, const std::vector<const Type *> &binding) const {
        if (p.typeOf >= 0 && static_cast<std::size_t>(p.typeOf) < binding.size() &&
            binding[p.typeOf] != nullptr) return binding[p.typeOf];
        if (p.builtOnParam) return substituteParams(p.type, binding);
        if (!resolvedParamTypes_.empty()) {
            const auto r = resolvedParamTypes_.find(std::make_pair(p.type, binding));
            if (r != resolvedParamTypes_.end()) return r->second;
        }
        return p.type;
    }
    // **`typename enable_if<C, int>::type = 0`** - a non-type parameter whose type depends on the parameters
    // before it, read again from its tokens with those bound; a type that cannot be formed fails the
    // substitution ([temp.deduct]/8). What it gives is kept for paramTypeIn under the same binding.
    const Type *resolveParamType(const std::vector<TemplateParam> &params, std::size_t i,
                                 const std::vector<const Type *> &binding, const std::vector<long long> &values);
    mutable std::map<std::pair<const Type *, std::vector<const Type *> >, const Type *> resolvedParamTypes_;
    const Type *substituteParams(const Type *t, const std::vector<const Type *> &binding) const;
    // **An address or a member pointer bound to a non-type parameter** ([temp.arg.nontype]/1, R1): what it
    // names, kept so each use rebuilds it in the function reading it - a member function's pair lives in that
    // frame. A parameter bound to one has its index here as its value.
    struct AddressArg {
        enum Kind { Null, Object, Function, DataMember, MemberFunction };
        Kind kind = Null;
        std::string spelling;            // the key: `&gv`, `str`, `gv`, `&S::m`, `nullptr`
        std::string key;                 // the name it is found under: `gv`, `ns::nv`, `S::sm`, `S::f`
        std::string symbol;              // the linker's name of the object or function
        std::string entity;              // the name a template argument spells, TemplateArg::entity
        const Type *type = nullptr;      // the object's, function's or member's type
        const Type *cls = nullptr;       // a member's class
        int offset = 0;                  // a data member's
        std::size_t function = 0;        // a member function's Signature
        bool readOnly = false;
        bool staticMember = false;
    };
    std::vector<AddressArg> addressArgs_;
    static bool takesAddress(const Type *t) {
        const Type *u = t->unqualified();
        return u->isPointer() || u->isReference() || u->isMemberPointer() || u->isMemberFunctionPointer() ||
               u->isNullPtr();
    }
    long long addressArgument(const Type *pt, std::size_t pos);
    void describeAddress(TemplateArg &a) const;
    ExprPtr addressExpression(long long index, const Type *paramType, std::size_t pos);
    // What is known about a name that names a template. `start` is the
    // `template` keyword: instantiation here will be a replay of these
    // tokens, so where they begin is the thing worth keeping.
    struct TemplateDecl {
        std::string name;
        // Its entry in `templates_`: the name itself, or for a template of a name another namespace had first,
        // `ns::name` (`::name` at global scope) - which also begins the tags of its specializations.
        std::string key;
        // The namespace it was declared in, `std::` included. Carried to the
        // specialization for the manglers alone - see Type::templateNamespace.
        std::string ns;
        std::vector<TemplateParam> params;
        bool isClass = false;
        // `template <class T> void gen(T) = delete;`: defined, and each specialization it makes deleted.
        bool isDeleted = false;
        // Written `class` rather than `struct`: a specialization made while the
        // template is only declared has to describe itself the way it was declared.
        bool classKey = false;
        bool defined = false;
        // `template <class T> using Ptr = T *;` - kept with the class templates, its type-id at aliasAt.
        bool isAlias = false;
        std::size_t aliasAt = 0;
        // **A pattern's stand-in for template template parameter `ttParam`**, entered under the
        // parameter's name while a pattern is read; -1 for every template a program declares.
        int ttParam = -1;
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
            // A constructor, replayed like a held inline one from `start`, the `Box` after `Box<T>::`.
            bool constructor = false;
            // **A static data member, not a member function.**
            bool isData = false;
            // A member of a nested class, `Tree<T>::It::Deep::count`: "It::Deep", under the specialization.
            std::string nested;
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
                // `R...` last: the pack takes the rest (`L<T, R...>`)
                bool isPackExpansion = false;
            };
            std::vector<Arg> args;
            std::size_t bodyAt = 0;      // the '{' of its class body
            std::size_t pos = 0;
        };
        std::vector<Partial> partials;

        // ---- A member function template ----
        // `template <int M> CVector<M> head() const { ... }` written inside a
        // class. A member class or alias template has ownerTag, the class*, ownerLocal and memberAccess too.
        bool isMember = false;
        Access memberAccess = Access::Public;
        std::string ownerTag;
        const Type *ownerType = nullptr;
        std::vector<TemplateParam> classParams;
        std::vector<const Type *> classBinding;
        std::vector<long long> classValues;
        Packs classPacks;
        // **A friend function template defined in its class** ([temp.friend], [class.friend]/6-7; 1.82): a
        // namespace's template, read in the class it was written in - its name, its statics, and a class
        // template's parameters bound as that specialization has them (classParams above).
        std::string friendOf;
        // **A constructor template**: named by its class's own name, keyed under constructorKey(ownerTag).
        bool isConstructor = false;
        // The class's name as the source spells it - `CMatrix` for `CMatrix<3,3>` - which inlineOwnerName_ wants.
        std::string ownerLocal;
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
    // `made` collects the symbols of the specializations this use deduced: [temp.over]/1 makes those its
    // candidates, and not one an earlier use left in the set.
    void instantiateViableTemplates(const std::vector<TemplateDecl> &overloads,
                                    const std::vector<const Type *> &argTypes, std::size_t pos,
                                    std::set<std::string> *made = nullptr);
    // The function templates a call of `name` names, [basic.lookup.qual] and [basic.lookup.unqual]:
    // the nearest namespace declaring one - or the one a qualifier names - in `*ns`; else every one.
    // A `scope` is the namespace a qualifier resolved to, absolute: `m` for `m::twice(2)`.
    std::vector<TemplateDecl> templatesNamed(const std::string &name, const std::string &scope,
                                             std::string *ns) const;
    // The same, for a member function template of `cls`.
    void instantiateViableMemberTemplates(const Type *cls,
                                          const std::string &name,
                                          const std::vector<const Type *> &argTypes,
                                          std::size_t pos);
    // The same by the table's own key - "CMatrix<3,3>::CMatrix<3,3>" for a constructor template.
    void instantiateViableMemberTemplatesUnder(const std::string &key,
                                               const std::vector<const Type *> &argTypes,
                                               std::size_t pos);
    // Every constructor template of `cls` that deduces from these argument types,
    // instantiated so the constructor set that is then resolved holds it.
    void instantiateConstructorTemplates(const Type *cls,
                                         const std::vector<const Type *> &argTypes,
                                         std::size_t pos);
    void instantiateConstructorTemplates(const Type *cls,
                                         const std::vector<ExprPtr> &args, std::size_t pos);
    // Member function templates by "<ownerTag>::<member>", every one of a name (operator* twice).
    std::map<std::string, std::vector<TemplateDecl> > memberTemplates_;
    // An instantiation made, keyed by the template's own position and its arguments.
    std::map<std::string, std::size_t> memberTemplateMade_;
    // One whose replay is under way, by the same key: the size functions_ had when it began.
    std::map<std::string, std::size_t> memberTemplateBusy_;
    // Set one-shot while a member-template specialization's body is replayed through declareMember:
    // the source member name, the display name with its arguments ("head<3>"), and those arguments,
    // so declareMember keys and mangles the specialization rather than the plain member.
    bool memberTemplateInst_ = false;
    std::string memberTemplateOf_;
    std::string memberTemplateName_;
    std::vector<TemplateArg> memberTemplateArgs_;
    Access memberTemplateAccess_ = Access::Public;
    // The pattern signature of the specialization being replayed, which the Itanium name is spelled from.
    const Type *memberTemplatePattern_ = nullptr;
    // Set once the specialization's body has begun: a failure after that is the body's, not a substitution's.
    bool memberTemplateBody_ = false;
    // Member template specializations declared whose bodies failed, by symbol: the error once one is chosen.
    std::map<std::string, std::pair<std::size_t, std::string> > failedBodies_;
    void refuseFailedBody(const Signature &chosen) const;
    // A constructor template specialization's C2 by its C1: a pattern's C2 cannot be recomputed from the substituted parameters.
    std::map<std::string, std::string> constructorC2_;
    // Constructors, by symbol, whose list initialises a virtual base: a delegating constructor's C1 cannot stand
    // in for theirs on Itanium.
    std::set<std::string> virtualBaseInitialisers_;
    // Member-template specializations made as *candidates*: one never chosen is emitted by neither oracle.
    std::vector<std::size_t> candidateSpecializations_;
    void pruneUnchosenCandidates(Program &program);
    // Inline functions the program wrote, emitted where defined and dropped at the end unless odr-used.
    std::set<std::string> discardableInline_;
    void pruneUnusedInline(Program &program);
    bool isMemberTemplate(const Type *obj, const std::string &name) const {
        return obj != nullptr &&
               memberTemplates_.count(obj->unqualified()->tag() + "::" + name) != 0;
    }
    // The class that declares `name` as a member template, `cls` or a base of it, or null.
    const Type *memberTemplateOwner(const Type *cls, const std::string &name) const {
        const Type *d = declaringClassOf(cls, name);
        return d != nullptr && isMemberTemplate(d, name) ? d : nullptr;
    }
    ExprPtr memberTemplateCall(ExprPtr object, const Type *obj,
                               const std::string &name, std::size_t pos);
    const Signature *instantiateMemberTemplate(
        const TemplateDecl &mt, const std::vector<const Type *> &binding,
        const std::vector<long long> &values,
        const std::vector<TemplateArg> &args, std::size_t pos);
    // **A template is keyed by its bare name, and named by either spelling.** Where namespaces share a
    // name, `sameNamed_` holds each one's key and the scope a name is read in chooses among them.
    std::map<std::string, std::vector<std::string> > sameNamed_;
    // What a using-declaration of a class template declared, `k::vector`, and the template's key.
    std::map<std::string, std::string> templateUsing_;
    std::map<std::string, TemplateDecl>::const_iterator
    templateInScope(const std::string &written, const std::string &bare, const std::vector<std::string> &keys) const;
    std::map<std::string, TemplateDecl>::const_iterator
    findTemplate(const std::string &written) const {
        const std::string::size_type cut = written.rfind("::");
        const std::string bare = cut == std::string::npos ? written : written.substr(cut + 2);
        const std::map<std::string, std::vector<std::string> >::const_iterator same = sameNamed_.find(bare);
        if (same != sameNamed_.end()) {
            const std::map<std::string, TemplateDecl>::const_iterator there = templateInScope(written, bare, same->second);
            if (there != templates_.end()) return there;
        }
        std::map<std::string, TemplateDecl>::const_iterator it = templates_.find(written);
        if (it != templates_.end()) return it;
        if (cut == std::string::npos) return templates_.end();
        return templates_.find(bare);
    }
    // The entry itself, for what changes one.
    std::map<std::string, TemplateDecl>::iterator templateEntry(const std::string &written) {
        const std::map<std::string, TemplateDecl>::const_iterator it = findTemplate(written);
        return it == templates_.end() ? templates_.end() : templates_.find(it->first);
    }
    // The key a declaration belongs under, a new one where another namespace has the bare name.
    std::string templateKeyFor(const TemplateDecl &decl);
    bool templateReachable(const TemplateDecl &decl, const std::string &written) const;
    bool templateQualifiedBy(const std::string &written) const;
    // The key of the template a specialization, or a template's name as a type, was made from.
    std::string templateKeyOf(const Type *t) const;
    static std::string tagName(const TemplateDecl &decl) { return decl.key.empty() ? decl.name : decl.key; }
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
        std::string templateKey;            // its entry in `templates_`
        std::string display;                // "ns::twice<int>": what a debugger shows for it (1.29)
        std::string symbol;
        const Type *fn = nullptr;           // the substituted signature
        std::vector<const Type *> binding;  // by parameter index
        std::vector<long long> values;      // non-type arguments, same index
        Packs packs;   // same index again
        std::size_t start = 0;              // the template's own tokens
        // A function template's namespace: `templates_` holds one entry per name, the first namespace's, and
        // `A::twice<8>` was read in `Z`, where the `twice` declared first was (1.82).
        std::string ns;
        bool ownNamespace = false;
        // A friend template's own declaration: each specialization of its class wrote one (1.82).
        std::shared_ptr<const TemplateDecl> friendDecl;
        std::size_t pos = 0;                // where it was first asked for
        std::vector<Source::Context> chain; // and the instantiations that asked
        // How many replayed bodies deep the request came from: one more than the specialization whose
        // body asked for this one, 1 from the program itself - bounded by kMaxNesting (2026-10-21, fix 5).
        int depth = 1;
        bool emitted = false;
        // A class specialization instead of a function one.
        bool isClass = false;
        // Written out rather than made, so there are no parameters to bind and no primary template to replay.
        bool explicitly = false;
        // From a partial specialization: the primary's out-of-line members are not its (A16).
        bool fromPartial = false;
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
        // Out-of-line definitions replayed so far (a list: more can come)
        std::vector<bool> outsideDone;
    };
    std::vector<Specialization> specializations_;
    // Set while a specialization's definition is being replayed: the name the
    // function must be given, and the symbol it must carry. One-shot, taken
    // by the declarator that names the function.
    std::string instantiationKey_;
    std::string instantiationOf_;
    // And its display name, Specialization::display, for the Function made (1.29).
    std::string instantiationDisplay_;
    // The depth of the specialization whose body is being replayed, 0 outside one: what a
    // specialization asked for from inside it is one deeper - see Specialization::depth.
    int instantiationDepth_ = 0;
    // A `>` inside a template argument list closes it and is not an operator
    // - [temp.names], and the reason a comparison there must be parenthesised.
    // Cleared inside parentheses, where a `>` is an operator again.
    bool inTemplateArgs_ = false;
    const std::string &instantiationName(const std::string &name) const {
        return (!instantiationKey_.empty() && name == instantiationOf_)
                   ? instantiationKey_ : name;
    }
    ExprPtr templateCall(Program *program, const std::string &scope = std::string());
    // A static member reached through a class template-id, after its `::`.
    ExprPtr templateIdMember(const Type *cls, std::size_t pos);
    // `Box<int, 3>` where a type was expected.
    const Type *instantiateClass(const TemplateDecl &decl, std::size_t pos);
    // The same with its arguments read.
    const Type *instantiateClassWith(const TemplateDecl &decl, const std::vector<const Type *> &binding,
                                     const std::vector<long long> &values, const std::vector<TemplateArg> &args,
                                     const Packs &packs, std::size_t pos);
    // [temp.inst]/1: a specialization named while its template was only declared - `typedef
    // basic_ostringstream<char> ostringstream;` in <iosfwd> - is instantiated where its name is reached once the
    // template is defined (realizeMemberClass), as a template-id written then would be. Kept by its tag with the arguments it was named with.
    struct PendingSpecialization {
        std::string templateName;           // its entry in `templates_`
        std::vector<const Type *> binding;
        std::vector<long long> values;
        std::vector<TemplateArg> args;
        Packs packs;
        bool deferred;  // put off by instantiateClass: template not defined yet
    };
    std::map<std::string, PendingSpecialization> pendingSpecializations_;
    // The class template specializations being read, one inside another.
    int classesInstantiating_ = 0;
    // Those put off while they were read, made once the outermost is done.
    void instantiateDeferredSpecializations(std::size_t pos);
    // [temp.mem]/1: the member class or alias template `name` of `cls` or a base of it, kept under
    // "<class tag>::<name>", or null. Unqualified, from the classes being read here, innermost first.
    const TemplateDecl *memberClassTemplateOf(const Type *cls, const std::string &name) const;
    const TemplateDecl *memberClassTemplate(const std::string &name) const;
    // The class a member class or alias template belongs to, by its tag.
    const Type *ownerTypeOf(const TemplateDecl &decl) const;
    // [class.access]/1 for one named from outside its class: private, or protected and not from a derived class.
    void checkMemberTemplateAccess(const TemplateDecl &decl, std::size_t pos) const;
    // [temp.names]/4: whether the nested-name-specifier closed by the `::` at `colon` depends on a template
    // parameter bound by the replay reading it - `T::` or `O<T>::`.
    bool qualifierDependsOnParameter(std::size_t colon) const;
    // **[temp.inst]/1: a class template's specialization declares its member classes and defines none.** Each
    // is kept here by its tag, with the bindings its definition is read with, and defined where a type name
    // first reaches it (realizeMemberClass) - or by an explicit specialization written before that.
    struct LazyMemberClass {
        std::size_t start = 0;          // the token after the class key
        Kind kind = Kind::Struct;
        bool isClass = false;
        const Type *owner = nullptr;
        std::vector<TemplateParam> params;
        std::vector<const Type *> binding;
        std::vector<long long> values;
        Packs packs;
        std::vector<std::string> namespaces;
        bool done = false;
        // Defined by an explicit specialization: the template's members of it written outside are not its.
        bool replaced = false;
    };
    std::map<std::string, LazyMemberClass> lazyMemberClasses_;
    // The member class being defined from its record, which is not deferred again.
    std::string realizingMember_;
    // Named while its class's own body is read, it stays declared unless `required` - a data member's type,
    // a base, sizeof. The element of an array, under its cv, is the class asked about.
    const Type *realizeMemberClass(const Type *t, bool required = false);
    // The packs of the class template being instantiated, by instantiatingParams_, as they are bound now.
    Packs instantiatingPacks() const;
    // `template <> struct S<int>::In { ... };` - [temp.expl.spec]/1 for a member class, at the `<` after S.
    void memberClassSpecialization(const TemplateDecl &primary, Kind kind, bool isClass, std::size_t pos);
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
    int probeVariant_ = -1;
    std::set<std::size_t> probeSkip_;
    ProbeResult probeResult_;
    std::vector<TemplateDecl> probeTargets() const;
    void probeUninstantiated(Program &program);
    bool probeFailureIsTheTemplates(const TemplateDecl &d, std::size_t pos, const std::string &why) const;
    // Inside the type specifier `typename X::m` begins: m has to be a type.
    bool typenameNamed_ = false;

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
        // **And so does being inside a template argument list**: a candidate given too many arguments
        // fails before its list resets it, and every `>>` read after was a closing angle.
        bool inArgs;
        // **And the packs**: a signature read for deduction names its function parameter pack, `A &&... a`,
        // and the reading that follows must not find `a` a pack. Held by a type declared further down.
        struct PacksCopy;
        PacksCopy *packs;
        explicit Trial(Parser *parser);
        ~Trial();
        Trial(const Trial &) = delete;
        Trial &operator=(const Trial &) = delete;
    };
    const Signature &instantiate(const TemplateDecl &decl,
                                 const std::vector<const Type *> &binding,
                                 const std::vector<long long> &values,
                                 const std::vector<TemplateArg> &args,
                                 std::size_t pos,
                                 const Packs &packs =
                                     Packs());
    void instantiatePending();
    // Past kMaxNesting bodies deep, a specialization asked for from inside another's body is refused.
    void refuseInstantiationDepth(const std::string &name, int depth, std::size_t pos) const;
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
                                        const Packs *packs = nullptr,
                                        bool *construction = nullptr);
    // The type parameters bound now, counted: `X::type` as a type needs `typename` (A27).
    std::map<std::string, int> boundTypeParams_;
    // The non-type parameters bound now, by name: in their template's scope they hide an enclosing one.
    std::map<std::string, int> boundValueParams_;
    // **A pack while a specialization is being read**: the types it was given and, once its function
    // parameters have been made, the names they were given - `rest` becomes `rest$0` and `rest$1`, and
    // `rest...` in a call is those two names. A non-type pack holds its values beside, of `valueType`.
    struct PackBinding {
        std::vector<const Type *> types;
        std::vector<std::string> names;
        bool isValue = false;
        const Type *valueType = nullptr;
        std::vector<long long> values;
        // A function parameter pack's, `a` of `Args &&... a`: recorded by its written name and left there after.
        bool functionParameter = false;
        // A pack of template template parameters: its members are templates' names, of this list.
        bool isTemplate = false;
        std::vector<TemplateParam> inner;
        bool standsForItself() const {
            return types.size() == 1 && types[0]->kind() == Kind::TemplateParam;
        }
    };
    // What a binding does to the two name tables, and how to put them back.
    struct Shadow {
        std::string name;
        bool isType = true;
        bool isPack = false;
        bool had = false;
        std::size_t was = 0;
        PackBinding hadPack;
        // A non-type parameter bound as a pattern reference rather than a value
        // - `N` in `V<N>` while a signature is read for deduction.
        bool isParamRef = false;
        bool hadParamRef = false;
        int paramRefWas = 0;
        // A template template parameter is an entry in `templates_` under its name; this is what it hid.
        bool isTemplate = false;
        int hiddenBound = 0;
        bool hadTemplate = false;
        TemplateDecl hadDecl;
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
                                const Packs &packs,
                                std::vector<Shadow> *undo);
    // [temp.deduct.type] rather than [temp.deduct.call]: this matches a template
    // *argument* against a pattern, where nothing decays and nothing may differ.
    // Deduction from a call is looser, because a conversion may still bridge it.
    bool matchPattern(const Type *pattern, const Type *arg,
                      std::vector<const Type *> *binding,
                      std::string *why) const;
    // While a partial specialization is matched: where `T[N]` puts N and `R(As...)` puts As.
    mutable std::vector<long long> *matchValues_ = nullptr;
    mutable Packs *matchPacks_ = nullptr;
    // `R(Args...)` as a template argument, its parameter list ahead.
    const Type *functionTypeArgument(const Type *base);
    // The partial specialization these arguments ask for, or npos for none.
    // Refuses by name where two match and neither is more specialized.
    std::size_t choosePartial(const TemplateDecl &decl,
                              const std::vector<TemplateArg> &args,
                              std::vector<const Type *> *binding,
                              std::vector<long long> *values,
                              Packs *packs,
                              std::size_t pos);
    // [temp.class.order]: whether one pattern is at least as specialized as
    // another, which is asked by matching each against the other.
    bool atLeastAsSpecialized(const TemplateDecl::Partial &a,
                              const TemplateDecl::Partial &b) const;
    bool moreSpecialized(const TemplateDecl::Partial &a,
                         const TemplateDecl::Partial &b) const;
    static bool mentionsPackExpansion(const Type *t);
    // `Box<T *>` after `template <class T> struct` - the argument list of a
    // partial specialization, read as patterns.
    void partialArguments(TemplateDecl::Partial *ps,
                          const std::vector<TemplateParam> &primary);
    void unbindTemplateParameters(const std::vector<Shadow> &undo);
    // An object declared under a function parameter pack's old name is no pack: `a` of `cat2(const tuple<A...> &a)`
    // read after a `call(Args &&... a)` was expanded as that one's members.
    void dropStalePack(const std::string &name);
    // **Parameters bound for a scope come off however it ends**: a substitution that fails throws past an
    // unbind written after it, and what called it then read its own parameters as the failed one's.
    struct Bound {
        Parser *p;
        std::vector<Shadow> undo;
        explicit Bound(Parser *q) : p(q) {}
        ~Bound() { p->unbindTemplateParameters(undo); }
        void release() { p->unbindTemplateParameters(undo); undo.clear(); }
    };
    // Deduction: the arguments worked out from the call rather than written.
    // Answers false and fills `why` when some parameter cannot be deduced.
    bool deduceTemplateArguments(const TemplateDecl &decl,
                                 const std::vector<const Type *> &argTypes,
                                 std::vector<const Type *> *binding,
                                 std::vector<long long> *values,
                                 Packs *packs,
                                 std::string *why,
                                 const std::vector<const Type *> *givenTypes = nullptr,
                                 const std::vector<long long> *givenValues = nullptr);
    // `values` takes a deduced non-type argument (the 3 in V<3>; may be null);
    // `nested`: inside a template-id, matched as is - no decay, the pattern's
    // missing cv deduced too ([temp.deduct.type]/8: V = const int).
    bool deduceOne(const Type *pattern, const Type *arg,
                   std::vector<const Type *> *binding,
                   std::vector<long long> *values, std::string *why,
                   Packs *packs = nullptr, bool nested = false) const;
    // An argument's type as a parameter sees it.
    const Type *decayedType(const Type *a) const;
    // ---- WS-E1 (review 2026-10-08): forwarding references, F30, F31, F34, alias templates ----

    // An argument's type for deduction: an lvalue of type U goes as `U &` ([temp.deduct.call]/3).
    const Type *deductionArgType(const Expr &e) const;
    // Is this pattern parameter `T &&` with T a bare template parameter - a forwarding reference?
    const Type *withoutCvOf(const Type *arg, const Type *pattern) const;
    static bool forwardingReference(const Type *pattern);
    // `[const] Ts [& | &&] ...`: the tokens it takes (0 if it is not one), where Ts is, and how a member is wrapped.
    std::size_t packDeclarator(int *wrap, std::size_t *nameAt) const;
    const Type *wrapPackMember(const Type *member, int wrap);
    // `f(g(a)...)`, `std::forward<A>(a)...`: one argument per member, the pattern read once for each.
    bool expandPackPattern(std::vector<ExprPtr> &args);
    // The same for `{ 0, Ns... }` in a braced list and `List<(Ns * 2)...>` in an argument list.
    struct Init;
    bool expandPackInitialiser(std::vector<Init> &items);
    bool expandPackValues(TemplateArg &a);
    // Where a pattern's `...` ends and how many members it expands to, and one member's tokens bound.
    std::size_t packPatternEnd(bool angle, std::size_t *members) const;
    void bindPatternMember(std::size_t from, std::size_t end, std::size_t m, std::vector<Shadow> *undo);
    void checkNonTypeArgument(const Type *pt, long long v, std::size_t argPos) const;
    // `template struct Box<int>;`, `template int twice<int>(int);` - [temp.explicit].
    bool explicitInstantiation(std::size_t pos);
    // `template <class U> U S::conv(U u) {...}` - a member template defined outside its class, [temp.mem]/1: kept
    // without its qualifier, under the class (or class template) and the member's name, with its parameters' names.
    struct OutOfLineMember { std::size_t copy = 0, copyEnd = 0; std::vector<std::string> innerNames, outerNames; };
    std::map<std::string, std::vector<OutOfLineMember> > outOfLineMembers_;
    bool outOfLineMemberTemplate(const std::vector<TemplateParam> &outer);
    void applyOutOfLineMemberTemplate(TemplateDecl *mt);
    std::set<std::string> explicitlyInstantiated_;
    // The members an explicit instantiation definition names, kept though nothing in this unit calls them.
    std::set<std::string> explicitlyInstantiatedSymbols_;
    // `template <> int twice<int>(int)` - F31.
    bool functionExplicitSpecialization(std::size_t pos);
    // `template <> int S<int>::f()` - R7.
    bool memberExplicitSpecialization(std::size_t pos);
    // `template <> template <> int S<int>::g(char)` and `template <> template <class U> int S<char>::g(U)`.
    bool memberTemplateExplicitSpecialization(std::size_t pos);
    // `namespace ns { extern "C" int cf(int); }`: cf under `ns::cf` too.
    void cFunctionInNamespace(const std::string &plain, std::size_t index);
    // A deduction left parameter `i` unbound: its default, if it has one, replayed with the earlier ones bound.
    bool defaultTemplateArgument(const TemplateDecl &decl, std::size_t i,
                                 std::vector<const Type *> *binding, std::vector<long long> *values, const Packs *packs = nullptr);
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
    StmtPtr initRangeLocal(const std::string &name, const Type *t, int off, ExprPtr init,
                           std::size_t pos);
    // Where a range-for's own objects became alive: a statement index and how many then.
    std::vector<std::pair<std::size_t, std::size_t> > rangeBuilt_;
    // The argument list at a use: `<int, 3>` read into types and values.
    void templateArguments(const TemplateDecl &decl,
                           std::vector<const Type *> *binding,
                           std::vector<long long> *values,
                           std::vector<TemplateArg> *args,
                           Packs *packs = nullptr,
                           std::size_t *given = nullptr);
    // **A name declared as an object is not a template name.**
    bool isTemplateName(const std::string &name, bool qualified = false) const {
        if (templates_.find(name) == templates_.end()) return false;
        if (qualified) return true;
        // [temp.local]/6: a template's own parameter hides the name - `C()` in basic_string<C, ...> is its
        // char, whatever template a program calls C.
        if (boundTypeParams_.count(name) != 0 || boundValueParams_.count(name) != 0) return false;
        // [basic.lookup.unqual]/8: a class's data member hides the namespace's - `max` beside std::max.
        for (std::size_t i = classStack_.size(); i-- > 0; )
            if (classStack_[i]->findStaticMember(name) != nullptr || classStack_[i]->findMember(name) != nullptr) return false;
        if (currentClass_ != nullptr &&
            (currentClass_->findStaticMember(name) != nullptr || currentClass_->findMember(name) != nullptr)) return false;
        return findLocal(name) == nullptr && findGlobal(name) == nullptr;
    }
    bool templateDeclaration();
    // `allowUnnamed` for a template template parameter's own list, whose names nothing refers to.
    void templateParameters(std::vector<TemplateParam> &params, bool allowUnnamed = false);
    // ---- Template template parameters, [temp.param]/1 and [temp.arg.template] ----
    // The stand-in a template template parameter is bound to: the template's name as a Type.
    const Type *templateNameTypeOf(const TemplateDecl &decl) const;
    // The template a specialization is of, as a binding: its stand-in, or a pattern's own parameter.
    const Type *templateOf(const Type *spec) const;
    // `$TT0`: a pattern's stand-in for template template parameter 0, and which one it is.
    static bool templateParamRef(const std::string &name, int *index);
    // [temp.arg.template]/3 as clang applies it under -std=c++11: whether a template with
    // parameters `arg` can take the argument list the parameter's own list `param` forms.
    static bool templateTemplateMatches(const std::vector<TemplateParam> &param,
                                        const std::vector<TemplateParam> &arg, std::string *why,
                                        std::size_t *at);
    // The argument written for a template template parameter: a class or alias template's name, or a bound parameter's.
    const TemplateDecl &templateTemplateArgument(const TemplateDecl &decl, const TemplateParam &p);
    // The arguments with each dependent non-type parameter's declared type, for Itanium's `Tn`.
    std::vector<TemplateArg> declaredArguments(const std::vector<TemplateParam> &params,
                                               const std::vector<TemplateArg> &args) const;
    // The argument list a deduction's binding amounts to, one TemplateArg per parameter.
    std::vector<TemplateArg> argumentsFromBinding(const std::vector<TemplateParam> &params,
                                                  const std::vector<const Type *> &binding,
                                                  const std::vector<long long> &values,
                                                  const Packs &packs) const;
    // What the declaration after `template <...>` actually declares: a class template, a
    // function template, or a member of a class template defined outside it.
    // `qualifier` is the class for that last one and empty otherwise.
    std::string templatedName(const std::vector<TemplateParam> &params,
                              bool *isClass, std::string *qualifier,
                              bool *construction = nullptr, const Type **pattern = nullptr);
    // [temp.param]/11: the first parameter after a pack, or npos; and the class template's refusal of one.
    static std::size_t afterPack(const std::vector<TemplateParam> &params);
    void refuseAfterPack(const std::vector<TemplateParam> &params);
    void requireDeducibleAfterPack(const TemplateDecl &decl, const Type *fn);
    // `sawInit`, when given, says the declaration ended at a `;` having passed
    // an `=` at depth zero - which is a static data member of a class template
    // being *defined* out of line, not a member left undefined.
    bool skipTemplatedDefinition(bool *sawInit = nullptr, bool *sawParen = nullptr);
    void skipTemplateArguments();
    // `One<Ts...>` - [temp.variadic]/4: a pack bound to its members expanded where the template's parameters are not
    // one pack. At the `<`: the list is copied with each such `P...` written out as names bound to its members, at_
    // is left at the copy, and *resumeAt and *resumeSplit say where the real list ends. False where nothing needs it.
    bool spliceArgumentPacks(const TemplateDecl &decl, std::vector<Shadow> *undo, std::size_t *resumeAt,
                             std::size_t *resumeSplit);
    std::size_t splicedNames_ = 0;
    // `Box<T>::Box(` or `Box<T>::~Box(`.
    bool atOutOfLineSpecial(std::string *what, std::string *nested = nullptr);
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
    std::string emitMemberPointerTypeInfo(const Type *ptr, std::size_t pos, std::string *why);
    std::string emitFunctionTypeInfo(const Type *fn, std::string *why);
    std::string emitPointerTypeInfo(const Type *ptr, std::size_t pos,
                                    std::string *why);
    std::string emitEnumTypeInfo(const Type *e, std::size_t pos, std::string *why);
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
    // **A vtable entry, a typeinfo field and the vptr are one pointer wide**
    // - eight bytes on the 64-bit targets, four on the C6000 - and an offset
    // stored among them (offset-to-top, a vbase_offset) is as wide, signed.
    int pointerBytes() const { return target_.sizeOf(Kind::Pointer); }
    const Type *ptrdiffType() const {
        return types_.get(pointerBytes() == 8 ? Kind::LongLong : Kind::Int);
    }
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
    // `PB::v` inside a member function: `(*this).PB::v`, or null where there is no such `this` (1.27).
    ExprPtr qualifiedMemberThroughThis(const Type *cls, const Member &m,
                                       const std::string &member, std::size_t pos);
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
    struct Alive;
    // The `if (mostDerived)` a Microsoft constructor opens with: the vbtable pointers and the virtual
    // bases, built only by the complete object. With `vbs`, a count of those built is zeroed first and
    // `vbs` gets each one's destruction on that count, for a region over the rest, [except.ctor]/3.
    std::vector<StmtPtr> guardedVirtualBaseInit(
        const Type *type, const std::string &cls, int thisSlot, int flagSlot,
        std::size_t pos, int srcSlot, bool moving,
        std::map<std::string, std::vector<ExprPtr> > *vbaseArgs,
        std::vector<Alive> *vbs = nullptr);
    // A thunk: a secondary table's entry for an override, moving `this` back
    // to the complete object (named for the offset, _ZThn16_N1C1gEv); `named`,
    // `returns` where known (a Microsoft adjustor's name, a deleting void *).
    std::string synthesizeThunk(const std::string &cls, const Type *type,
                                const VSlot &slot, int offset, std::size_t pos,
                                const std::string &named = std::string(), const Type *returns = nullptr,
                                bool vtordisp = false);
    // cl's vcall thunk: `&S::f` on a virtual f is the address of a function
    // that dispatches through the object's vftable, since the Microsoft
    // member pointer is one code pointer with no room for a slot index.
    std::string synthesizeVcallThunk(const Type *cls, const Signature &f,
                                     int index, std::size_t pos);

    // How well an argument matches a parameter, in [over.ics.scs]'s order -
    // compared as values, so do not reorder; the three UserDefined values are
    // one rank split by the second conversion, [over.ics.rank]/3.3.
    enum class Rank { Identity, Qualification, Promotion, Conversion, BooleanConversion,
                      UserDefined, UserDefinedPromotion, UserDefinedConversion,
                      Ellipsis, None };
    // The UserDefined rank an argument of class `from` has for a scalar `to` reached through
    // the class's one scalar conversion function; the plain rank for a class target.
    Rank userDefinedRank(const Type *from, const Type *to);
    // Two parameters that took one argument of type `given` at the same rank, [over.ics.rank]/3.2
    // and /4: +1 when a's conversion is the better one, -1 for b's, 0 where nothing tells them apart.
    int closerTarget(const Type *pa, const Type *pb, const Type *given) const;
    // `ranksObjectA`/`B` say whether that candidate's rank vector opens with an implicit object
    // parameter, which is how a rank position is mapped back to the parameter it came from - a
    // member and a non-member operator are ranked side by side and do not open the same way.
    bool betterCandidate(const std::vector<Rank> &a, const std::vector<Rank> &b,
                         const Signature &fa, const Signature &fb,
                         bool ranksObjectA, bool ranksObjectB) const;
    // [temp.func.order]: a's template beats b's when b's patterns match a's, not the reverse.
    bool moreSpecializedFunction(const Signature &a, const Signature &b) const;

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
        // The class before `::` in a definition (`Point::get` defines get)
        std::string qualifier;
    };

    enum StorageClass { StorageNone, StorageStatic, StorageExtern, StorageTypedef,
                        StorageRegister, StorageAuto };

    struct Qualifiers {
        bool isConst = false;
        bool isVolatile = false;
        // `constexpr` implies const on an object, so isConst is set with it and almost everything downstream needs to know nothing more.
        bool isConstexpr = false;
        bool constWritten = false;   // `const` itself among the specifiers, not only the one constexpr implies
        // `inline` on a function: its definition may appear in several
        // translation units, so it is emitted as a weak/COMDAT definition the
        // linker folds - the same treatment a template specialization gets.
        bool isInline = false;
        int alignAs = 0;   // `alignas(N)` among the specifiers: the largest, else 0
        bool isThreadLocal = false;   // `thread_local`, [dcl.stc]/4: one object per thread
        // `thread_local` on a target that runs one thread and has no thread storage (tms6747): the one
        // copy per thread is the one static copy, so the object gets static storage duration instead.
        bool threadLocalAsStatic = false;
    };

    struct TypedefName {
        std::string name;
        const Type *type;
    };

    struct EnumConst {
        std::string name;
        long long value;
        // The enumeration's type when an enum-base chose one, else null: int.
        const Type *type = nullptr;
        // A bound non-type template parameter's declared type, which only a signature's expression spells.
        const Type *paramType = nullptr;
        // The access of a class's enumerator, which is a member of the class.
        Access access = Access::Public;
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
    // The depth of scopeStarts_ inside the function body's own block, 0 outside one.
    std::size_t bodyScope_ = 0;
    int frameSize_ = 0;
    std::vector<FreeSlot> freeSlots_;    // slots closed scopes gave back - see leaveScope
    int localSerial_ = 0;
    const Type *returnType_ = nullptr;
    // The frame slot that holds the caller's address for the returned object, where the function returns
    // through a hidden pointer; 0 where it does not.
    int sretSlot_ = 0;
    // **A class whose copy is a call is returned where the caller asked for it** ([class.copy]/31-32): built
    // there by its constructor, or moved or copied there, and never moved by its bytes (1.81).
    StmtPtr returnInPlace(ExprPtr returned, std::size_t pos, bool leavesFunclet);
    // **The local a function returns by name is the caller's object** ([class.copy]/31, the named return value):
    // its slot holds the caller's address, as a by-value parameter's slot does, and it is built, used and
    // destroyed through it. The slot and the name while one is in scope; 0 and empty otherwise.
    int nrvoSlot_ = 0;
    std::string nrvoName_;
    bool namedReturnCandidate(const std::string &name, const Type *type) const;
    bool namedReturnInScope() const;
    // What a `return` keeps in a slot while destructors run: the address a reference return gives back.
    const Type *returnSlotType() { return returnType_->isReference() ? types_.pointerTo(returnType_->referent()) : returnType_; }
    // Set while a lambda's body is read to find its return type: a `return`
    // writes the type of its operand here, the first one deciding, and is
    // not checked against returnType_, there being none yet.
    const Type **deducingReturn_ = nullptr;
    std::string functionName_;
    std::vector<std::string> staticSymbols_;
    // Above 0 inside sizeof, decltype, noexcept: [expr.prim.general]/13 lets `S::x` go without an object.
    int unevaluated_ = 0;

    std::vector<Signature> functions_;
    // One name, every function declared under it. C had one; C++ has a set,
    // and the set is ordered by declaration so a diagnostic can list the
    // candidates in the order the reader wrote them.
    std::vector<Signature> &functionTable() { return functions_; }
    std::unordered_map<std::string, std::vector<std::size_t> > functionIndex_;
    std::vector<GlobalSym> globals_;
    std::unordered_map<std::string, std::size_t> globalIndex_;
    // The namespace-scope objects first declared with internal linkage, for [dcl.stc]/7.
    std::set<std::string> internalGlobals_;
    // The temporaries references with static storage bind, numbered for their Microsoft names.
    int staticTemps_ = 0;
    std::vector<TypedefName> typedefs_;
    std::unordered_map<std::string, std::size_t> typedefIndex_;
    // The access of a typedef or alias a class declares, keyed as typedefIndex_ is ("S::T").
    std::unordered_map<std::string, Access> memberTypeAccess_;
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
        long long count = 0;   // an array of this many `cls`, destroyed last first; 0 for one object
        // A subobject a constructor has built, [except.ctor]/3: `offset` is then the slot of `this`,
        // and the object `owner`'s member `member` or its base `base` - both -1 for the whole object.
        const Type *owner = nullptr;
        int member = -1, base = -1;
        int builtSlot = -1;   // an array being built: the slot counting its `cls` elements built so far
        // Destroyed only where the slot holds more than `whenAbove`: cl's virtual bases, built under a flag.
        int whenSlot = -1, whenAbove = 0;
        // That slot is a guard, cleared before the destructor runs: one that throws has ended the object.
        bool whenOnce = false;
    };
    // **A function written on its own, between the statements of another**: its pads are its own,
    // so the enclosing `try`'s chain and pad flag are set aside while it is built.
    struct OwnPads {
        Parser &p;
        std::string chain, handler;
        bool pads;
        explicit OwnPads(Parser &parser)
            : p(parser), chain(parser.tryChainLabel_), handler(parser.handlerResumeLabel_),
              pads(parser.functionHasPads_) {
            p.tryChainLabel_.clear();
            p.handlerResumeLabel_.clear();
            p.functionHasPads_ = false;
        }
        bool made() const { return p.functionHasPads_; }
        ~OwnPads() { p.tryChainLabel_ = chain; p.handlerResumeLabel_ = handler; p.functionHasPads_ = pads; }
    };
    // [except.ctor]/3 over an element loop: `loop` counts in `indexSlot` the elements of `a` built.
    StmtPtr guardElements(StmtPtr loop, Alive a, int indexSlot, std::size_t pos);

    // One automatic object a jump may not land past - see Local::guardsJump - or with `region`
    // a `try` block (1) or a handler (2), which [except.handle]/2 lets a jump leave and never enter.
    // Its serial is the identity: a slot may be shared, a name declared again in an inner block.
    struct JumpGuard { std::string name; int serial; int region = 0; };
    std::vector<JumpGuard> jumpGuards() const;
    void checkJump(const std::vector<JumpGuard> &from,
                   const std::vector<JumpGuard> &to, std::size_t pos,
                   const std::string &jump, const std::string &origin) const;

    struct SwitchCtx {
        std::vector<const Case *> cases;
        const Case *deflt;
        const Type *governing;
        // What was in scope at the `switch`, where every case jumps from
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
        // A goto's own: whether it destroys what it leaves (a parent's half of a funclet's jump
        // does not), and the exit a Microsoft handler leaves its funclet by.
        bool destroys = true;
        std::string msExit;
    };
    // `goto name;` as written here - what the jump destroys, ends and leaves is decided once the label is read.
    StmtPtr gotoStatement(const std::string &name, std::size_t pos, bool destroys);
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
    std::size_t groupCloseAt(std::size_t i) const;
    bool endsInEmptyCall() const;
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
    void checkMemberTypeAccess(const Type *cls, const std::string &name, std::size_t pos) const;
    void checkMemberEnumAccess(const Type *cls, const std::string &name, std::size_t pos) const;
    void checkNamedMemberAccess(const Type *owner, Access access, const std::string &name, std::size_t pos) const;
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
    // Whether the class declares a constructor - a constructor *template* counts, [class.ctor]/5,
    // though none of its specializations is in the table until something asks for one.
    bool hasConstructors(const std::string &cls) const {
        return overloadsOf(constructorKey(cls)) != nullptr ||
               memberTemplates_.count(constructorKey(cls)) != 0;
    }
    void declareConstructor(const std::string &cls, std::size_t pos, Access access,
                            bool isExplicit);
    void declareDestructor(const std::string &cls, std::size_t pos, Access access,
                           bool isVirtual);
    std::string deletingDestructorSymbol(const std::string &cls);
    // `type` where the class is not found by its name yet - an implicit destructor's, at the class's end.
    void registerDestructor(const std::string &cls, std::size_t pos,
                            Access access, bool isVirtual, bool implicit,
                            VirtSpecifiers virt = VirtSpecifiers(), const Type *type = nullptr);
    void declareImplicitDestructor(const std::string &tag, const Type *type,
                                   std::size_t pos);
    void synthesizeDestructor(std::size_t which);
    // [class.dtor]/8's member half, shared by the destructor the compiler
    // writes and the one the program writes.
    std::vector<StmtPtr> memberDestructors(const std::string &cls,
                                           const Type *type, int thisSlot,
                                           std::size_t pos);
    // A member of this type, or an array of one, has a destructor to run.
    bool memberHasDestructor(const Type *t) const;
    // One member's destruction, appended to `out` - nothing for a member with no destructor.
    void memberDestructor(std::vector<StmtPtr> &out, const std::string &cls, const Type *type,
                          int thisSlot, std::size_t n, std::size_t pos);
    // The base-object destructor of `cls`'s base `which`, on `this` - D2, or cl's `??1`.
    StmtPtr baseDestructorCall(const Type *cls, std::size_t which, int thisSlot, std::size_t pos);
    // The class's own base-object destructor, on `this` and not through its vtable.
    StmtPtr selfDestructorCall(const Type *cls, int thisSlot, std::size_t pos);
    // A constructor the compiler writes, `steps` in order with `done[i]` what step i finishes (no owner:
    // nothing) - each guarding the steps after it. Written on its own, so no enclosing `try` is its own.
    StmtPtr guardConstruction(std::vector<StmtPtr> steps, std::vector<Alive> done, std::size_t pos,
                              bool *guarded);
    // cl's virtual-base `init` in front of `rest`: the count zeroed, and the rest guarded by `vbs`.
    StmtPtr withVirtualBases(std::vector<StmtPtr> init, const std::vector<Alive> &vbs, StmtPtr rest,
                             std::size_t pos);
    // `rest` in one region destroying all of `as`, last first, on unwind.
    StmtPtr guardSubobjects(StmtPtr rest, const std::vector<Alive> &as, std::size_t pos);
    // [except.ctor]/3: `rest`, run after a subobject is built, in a region destroying it on unwind - and
    // `before` with it, which a pad handing over to a function try block's handlers owes as well.
    StmtPtr guardSubobject(StmtPtr rest, const Alive &a, std::size_t pos,
                           const std::vector<Alive> &before = std::vector<Alive>());
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
                                    *vbaseArgs = nullptr, bool delegating = false);
    // What a delegating constructor of a class with virtual bases calls, on Itanium: see delegatingCall.
    struct DelegationEntry { std::string entry, c1, c2; std::vector<const Type *> params; };
    std::vector<DelegationEntry> delegationEntries_;
    std::set<std::string> delegationEntriesMade_;
    void synthesizeDelegationEntry(const Type *type, const DelegationEntry &d, std::size_t pos);
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
                                              *vbaseArgs = nullptr,
                                          std::vector<int> *builtBase = nullptr);
    static std::string destructorKey(const std::string &cls) {
        return cls + "::~" + localOf(cls);
    }
    // A constructor of this class taking nothing, written or implicit.
    const Signature *defaultConstructorOf(const Type *cls) const;
    enum { kImplicitDefault, kImplicitCopy, kImplicitMove, kImplicitCopyAssign, kImplicitMoveAssign };
    bool implicitNoexcept(const Type *type, int kind) const;
    // What the class did not write, the compiler declares. Called once the
    // class is complete, because whether an implicit member is trivial - and
    // so whether it is a function at all - is a question about the members.
    void declareImplicitSpecials(const std::string &tag, const Type *type,
                                 std::size_t pos);
    void declareImplicitDefaultCtor(const std::string &tag, const Type *type,
                                    std::size_t pos, Access access);
    // Bodies for the implicit members something actually called, run at the
    // end of the file and to a fixed point: giving one class a body can be
    // what first calls another's.
    void defineImplicitFunctions();
    void synthesizeDefaultCtor(std::size_t which);
    // `using B::B;` [class.inhctor]: the bases a class inherits constructors from, and per declared
    // constructor the base and the base constructor (an index into functions_) it forwards to.
    std::map<std::string, std::vector<const Type *> > inheritFrom_;
    struct Inheriting { const Type *base; std::size_t from; };
    std::map<std::size_t, Inheriting> inheriting_;
    // An inherited constructor's symbol to the class that first declared it, which both ABIs' names
    // and a further `using D::D;` spell: `E : D : B` makes E's `ECI11BEi`, and its body calls `DCI21BEi`.
    std::map<std::string, const Type *> inheritedFrom_;
    void declareInheritingCtors(const std::string &tag, const Type *type, std::size_t pos);
    // `using B::m;` in a class: the functions it names, joined once the class is complete; and the access
    // it gives, by "<tag>::<name>", which is m's as a member of that class ([namespace.udecl]/15).
    struct ClassUsing { std::string name; const Type *base; std::size_t pos; };
    std::map<std::string, std::vector<ClassUsing> > classUsings_;
    std::map<std::string, Access> usingAccess_;
    void classUsingDeclaration(const std::string &tag, const Type *type, Access access);
    void declareUsingMembers(const std::string &tag);
    // The class from `cls` down to `decl` whose using-declaration names `name`, nearest first, and
    // in `*out` the access it gives; null where none does.
    const Type *usingAccessOf(const Type *cls, const Type *decl, const std::string &name, Access *out) const;
    // The vtable, type_info and name string each class emitted, by tag, so the key-function rule can take them back.
    std::map<std::string, std::vector<std::string> > classSymbols_;
    bool keyFunctionUndefined(const std::string &tag) const;
    bool hasKeyFunction(const std::string &tag) const;
    const Signature *keyFunction(const std::string &tag) const;
    void pruneExternalVtables(Program &program);
    void markInlineBody(std::size_t which) { if (which != PendingBody::npos()) functions_[which].inlineBody = true; }
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
    StmtPtr eachElement(int indexSlot, ExprPtr count, StmtPtr one);
    // The two loops an array of a class needs, each a file-local function
    // synthesized once per class: `(T *base, size_t n)` building every
    // element with the default constructor, and destroying them last first.
    std::string vectorLoopName(const char *which, const Type *cls, std::size_t pos);
    std::string vectorConstructor(const Type *cls, std::size_t pos);
    std::string vectorDestructor(const Type *cls, std::size_t pos);
    ExprPtr callVectorLoop(const std::string &fn, const Type *cls, ExprPtr base,
                           ExprPtr count, std::size_t pos);
    // The bytes before an array from `new T[n]` that hold n, for delete[].
    int arrayCookie(const Type *elem) const;
    std::vector<StmtPtr> buildStaticArrayConstruction(Declared &d,
                                                      const std::string &symbol,
                                                      const std::string &helper);
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
        // Storage a new-expression allocated and is still building in: [expr.new]/20
        // gives it back through the matching deallocation function if that throws.
        bool newStorage = false;
        bool newArray = false, newNothrow = false;
        int newCookie = 0;
        // A listed element of `new T[n]{...}`, built at that index of the array the slot points at.
        bool newElement = false;
        long long newIndex = 0;
        // The slot is the array itself, a frame object, and not a pointer to it.
        bool elementInFrame = false;
        // Storage `new (a) T` took from a placement `operator new`: given back by the matching
        // placement `operator delete`, placementDeletes_[this], with the same arguments (R27).
        int placementDelete = -1;
        // From `::new`: given back to the global `operator delete`, not the class's.
        bool newGlobal = false;
        // `__cxa_allocate_exception`'s storage: freed, never destroyed
        bool exceptionStorage = false;
    };
    // [expr.new]/20: a placement `operator delete` matched to the `operator new` that allocated, and
    // where its arguments after the first were kept - evaluated once, for both.
    struct PlacementDelete {
        Signature fn;
        std::vector<std::string> names;
        std::vector<int> slots;
        std::vector<const Type *> types;
    };
    std::vector<PlacementDelete> placementDeletes_;
    // Set by userPlacementAllocation when it found one, for the guard the caller arms; -1 otherwise.
    int pendingPlacementDelete_ = -1;
    // `::new`, `::delete`: set by the `::` for the expression that follows, and held while it is read -
    // classAllocator then answers nothing, and the global functions allocate and free.
    bool globalNewDelete_ = false;
    bool globalAllocation_ = false;
    // `P()` of a class with no constructor written - its temporary named `$vinit` - as a constant.
    static bool valueInitialisedTemporary(const Expr &e);

    // **The ways out of a Microsoft handler**, one record per `try`: a funclet
    // cannot jump into its parent, so each early exit names a label the parent
    // lays down after the `try` and hands it back as the address to carry on at.
    struct MsExit {
        std::string ret, brk, cont;   // the labels, empty until an exit asks
        std::vector<std::pair<std::string, std::string> > gotos;   // an exit label, and the goto's
        int retSlot = 0;              // the returned value, saved for the parent
        std::string retTemp;
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
        int frameSize = 0, thisOffset = 0, frameAlign = 16;
        std::vector<FreeSlot> freeSlots;
        int localSerial = 0;
        const Type *currentClass = nullptr, *friendScope = nullptr;
        const Type *returnType = nullptr;
        int sretSlot = 0, nrvoSlot = 0;
        std::string nrvoName;
        const Type **deducingReturn = nullptr;
        int unevaluated = 0;
        int lambdaCount = 0;
        bool atFunctionBody = false;
        std::vector<Alive> alive;
        std::vector<Temporary> pendingTemps;
        // The block's statements' temporaries: a body compiled mid-statement - a constructor template made for
        // the initialiser being read - names its own, in its own frame.
        std::vector<Temporary> statementTemps;
        // The flags zeroed at its entry: a nested body's - a new-expression's in a constructor template made
        // mid-statement - are its own, at its frame's offsets.
        std::vector<int> guardSlots;
        std::size_t bodyCleanupFrom = 0;
        bool functionHasPads = false, functionHasTry = false;
        std::vector<std::string> functionTypes;
        bool variadicBody = false, inStaticMember = false, inParams = false;
        std::vector<std::string> staticSymbols;
        std::vector<std::size_t> pendingDefaults;
        bool pendingNoexcept = false;
        DynamicSpec pendingSpec;
        bool inNoexceptFunction = false;
        std::string dtorEndLabel;
        bool dtorReturned = false;
        bool replayingInline = false;
        bool currentFunctionInline = false;
        int tssSeq = 0;
        // The six a review found still missing, each with a program that
        // reached it: `break` and `case` written in a lambda inside a loop or
        // a switch were accepted and segfaulted the compiler.
        int loopDepth = 0, switchDepth = 0;
        std::vector<SwitchCtx> switches;
        std::vector<std::size_t> breakMarks;
        bool inTryBody = false, inMsHandler = false, inHandlerBody = false, inCtorTryHandler = false;
        MsExit *msExit = nullptr;
        int handlerDepth = 0;
        std::vector<int> handlerLoopDepth, handlerSwitchDepth;
        std::vector<std::size_t> handlerFrom, tryFrom;
        std::string handlerResumeLabel;
        int handlerResumePtr = 0, handlerResumeSel = 0;
        // The `try` being read: a body compiled mid-statement inside it has no try of its own, and its
        // cleanup rows must not name this function's chain.
        std::string tryChainLabel;
        int tryChainPointerSlot = 0, tryChainSelectorSlot = 0;
        std::size_t tryChainAliveFrom = 0;
        std::size_t handlerAliveFrom = 0;
        std::vector<Try *> tryBodySegments;
        int tryBodySerial = -1;
        int mayThrow = 0;
        bool conditionDecl = false;
        std::string conditionName;
        // Block-scope; a lambda body is a block (a directive leaked out)
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

    // **An unevaluated operand uses nothing** ([basic.def.odr]/2): what the outermost one marked as used is
    // put back as it leaves, as a trait's trial does it (holdUses, releaseUses) - a specialization named only
    // in a `sizeof`, a `decltype` or a `noexcept` was written into the program, and what it called had to
    // be defined for the link (1.77).
    struct Unevaluated {
        Parser *p;
        bool holds;
        std::vector<char> used;
        // Every one keeps and puts back its own: a mark made in an operand nested in another, or in a
        // trial, is not that outer one's to carry into a body read in between.
        explicit Unevaluated(Parser *pp) : p(pp), holds(true) {
            p->unevaluated_++;
            if (holds) p->holdUses(used);
        }
        ~Unevaluated() {
            if (holds) p->releaseUses(used);
            p->unevaluated_--;
        }
        Unevaluated(const Unevaluated &) = delete;
        Unevaluated &operator=(const Unevaluated &) = delete;
    };
    // [expr.unary.noexcept]/3 counts the potentially-evaluated calls: what an unevaluated operand inside
    // the operand would call is not one - `noexcept(sizeof(K(k)))` is true (1.77).
    struct NotCounted {
        Parser *p;
        int was;
        explicit NotCounted(Parser *pp) : p(pp), was(pp->mayThrow_) {}
        ~NotCounted() { p->mayThrow_ = was; }
        NotCounted(const NotCounted &) = delete;
        NotCounted &operator=(const NotCounted &) = delete;
    };

    // **How deep the parser's own recursion is**, at each level of nesting it
    // re-enters for; refused by name past kMaxNesting before the stack runs out
    // (256 MB reserved, 25-50 KB a level: 2048, twice [implimits]' 1024).
    static const int kMaxNesting = 2048;
    int nesting_ = 0;
    // How many constexpr calls may be in flight at once: twice the 512 [implimits] asks, refused by name past it.
    static const std::size_t kMaxConstexprCalls = 1024;
    struct Nesting {
        Parser *p;
        Nesting(Parser *pp, std::size_t pos, const char *what);
        ~Nesting() { p->nesting_--; }
        Nesting(const Nesting &) = delete;
        Nesting &operator=(const Nesting &) = delete;
    };

    FunctionState captureFunctionState() const;
    void restoreFunctionState(const FunctionState &s);
    void clearFunctionState();

    // `except` is the frame offset of an object not to destroy - the one being returned,
    // which the caller destroys instead. And a cleanup region's landing pad: destroy
    // alive_[from..to), last first, and hand the exception back to the unwinder.

    // A pad's steps as a block: all but the last (the resume, or the hand-over) in an inner block marked as unwinding.
    static StmtPtr unwindPad(std::vector<StmtPtr> steps);

    // The call that hands the exception in `pointerSlot` back to the unwinder, as this target's runtime takes it.
    StmtPtr resumeUnwinding(int pointerSlot);
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
    // **The same for thread storage** - [basic.stc.thread]: `__tls_init`, its frame and its guards,
    // built while tlsObject_ says the object being declared is a thread_local; the init functions
    // below write to initSink(), and registerDestruction asks the target's threadAtexit().
    std::vector<StmtPtr> tlsInit_;
    int tlsInitFrame_ = 0;
    std::vector<int> tlsInitGuards_;
    bool tlsObject_ = false;
    std::vector<StmtPtr> &initSink() { return tlsObject_ ? tlsInit_ : dynInit_; }
    // The external thread_locals this unit defines, those __tls_init builds (external and internal),
    // and those it reaches: what the Itanium wrappers and `_ZTH` aliases are made from.
    std::set<std::string> tlsDefined_, tlsDynamic_, tlsReached_, tlsInternalDynamic_, tlsHere_;
    void tlsDefining(const std::string &symbol, bool external);
    void finishTlsInit(Program &program);
    // The flags a Var reaching a thread_local carries, see Var::viaWrapper.
    void tlsVar(Var *v, bool viaWrapper, bool external);
    // A block-scope static just declared is a thread_local: marked, and taken out of the debug records.
    void markThreadLocal(std::size_t local, bool threadLocal);
    // **The widest alignment a slot of the function being parsed asked for**, 16 until one asks for
    // more; kept by the function's symbol for parse() to hand each Function, since fifteen sites
    // build one and the backends realign the frame from this.
    int frameAlign_ = 16;
    std::map<std::string, int> frameAligns_;
    // What a namespace-scope declaration says of its object's thread storage, recorded on the
    // symbol and checked against what an earlier declaration said - [dcl.stc]/4, clang's words.
    void noteThreadLocal(GlobalSym &g, bool prior, bool threadLocal, const std::string &name,
                         bool viaWrapper, bool external, bool defines, std::size_t pos);
    // `thread_local` written where only a variable may carry it - clang's words.
    void refuseThreadLocalHere(bool written, std::size_t pos);
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
    void dynamicInitialise(Declared &d, const std::string &symbol,
                           const std::string &helper, bool once, std::vector<GlobalPiece> *folded = nullptr);
    // [stmt.dcl]/4.
    StmtPtr guardOnce(const std::string &symbol, std::vector<StmtPtr> body);
    StmtPtr guardOnceThread(const std::string &symbol, std::vector<StmtPtr> body);
    // Where guardSlots_ stood when the init function's frame was entered.
    std::size_t initGuardMark_ = 0;
    void staticLocalWithConstructor(Declared &d,
                                    std::vector<StmtPtr> &inits);
    // With its type, an inline function's static local takes the ABI's name (1.28).
    std::string uniqueStaticSymbol(const std::string &name, const Type *type = nullptr);
    // Whether that symbol is one - the per-unit `owner.name` has a dot, the ABI's names have none -
    // so that it and its guard are external and in a COMDAT, one object for the program.
    bool sharedStaticLocal(const std::string &symbol) const {
        return currentFunctionInline_ && symbol.find('.') == std::string::npos;
    }
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
                             const Declared &d, Access access,
                             bool threadLocal, bool isConstexpr = false);
    void defineConstexprStaticMember(const std::string &full, const std::string &symbol, const Declared &d);
    std::set<std::string> inClassConstexprStatics_;
    void defineStaticMember(Declared &d, Program &program, bool threadLocal);
    // **What a static member *defined* out of line is worth when read** - the
    // [expr.const]/3 read-back a namespace-scope const already makes.
    struct StaticConst {
        bool known = false;   long long value = 0;
        bool dknown = false;  long double dvalue = 0;
    };
    std::map<std::string, StaticConst> staticConsts_;
    // The static data members an evaluated expression named, by symbol: a class template's
    // is defined only where one of these says so - [temp.inst]/3.
    std::set<std::string> usedStaticMembers_;
    const StaticConst *findStaticConst(const std::string &symbol) const {
        std::map<std::string, StaticConst>::const_iterator i =
            staticConsts_.find(symbol);
        return i == staticConsts_.end() ? nullptr : &i->second;
    }
    ExprPtr staticMemberRef(const Type *owner, const Type::StaticMember &s,
                            const std::string &cls, std::size_t pos);
    // `S::x` for a non-static data member, in an unevaluated operand: an lvalue of its type.
    ExprPtr memberWithoutObject(const Type *cls, const Member &m, std::size_t pos);
    ExprPtr unevaluatedMember();
    void declareMember(const std::string &cls, const Declared &d, bool constThis,
                       RefQual refThis, Access access, bool inUnion, bool isVirtual,
                       bool isStatic = false, bool isPure = false,
                       VirtSpecifiers virt = VirtSpecifiers());
    // [class.virtual]/4: the slot a member is about to take must not be `final`.
    void checkNotFinal(const VSlot &s, const std::string &cls, const std::string &name,
                       std::size_t pos) const;
    // The entry a pure virtual's slot holds: the runtime routine that reports
    // a call reaching a function the class never defined. Both measured -
    // `__cxa_pure_virtual` from clang, `_purecall` from cl.
    void checkNotAbstract(const Type *t, std::size_t pos, const std::string &what);
    const char *pureVirtualSymbol() const {
        return target_.microsoftNames() ? "_purecall" : "__cxa_pure_virtual";
    }
    // A deleted virtual's slot: `__cxa_deleted_virtual` from clang, `_purecall` again from cl. By symbol.
    std::set<std::string> deletedVirtuals_;
    const char *deletedVirtualSymbol() const {
        return target_.microsoftNames() ? "_purecall" : "__cxa_deleted_virtual";
    }
    // [class.virtual]/14: a deleted function and one that is not do not override each other; and
    // [except.spec]/5: an overrider allows no exception the function it overrides does not.
    void checkOverride(const Type *type, const Signature &f, bool deleted, std::size_t pos) const;
    std::string memberSymbol(const std::string &cls, const std::string &name,
                             const Type *fn, Access access, unsigned cvThis,
                             RefQual refThis, std::size_t pos, bool isVirtual = false,
                             bool isStatic = false);
    // `S a[4];` - the default constructor once per element, and `S a[3] = { ... };` - each element
    // from its initialiser, the rest value-initialised. Apart from constructLocal, which builds a
    // single object and names the class through d.type, which for an array is the array.
    StmtPtr constructLocalArray(const Declared &d, int offset, int indexSlot);
    StmtPtr constructLocalArrayFromList(Declared &d, int offset,
                                        const std::function<int()> &declareLate,
                                        const std::string &symbol = std::string());
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
    // The class a qualified name's prefix `q` names, one component past `prev`'s: by its key, or as a member type.
    const Type *memberClassAlong(const std::string &q, const Type *prev, const std::string &component) const {
        if (const Type *t = findTypedef(q)) return t;
        return prev != nullptr && prev->isStructOrUnion() ? lookupInClass(prev, component) : nullptr;
    }
    // Whether the class being parsed, or the one whose member function is being parsed, is this class or something derived from it.
    bool insideClass(const Type *cls) const;
    // **[class.access]/6**.
    bool definesMemberOf(const Type *cls, std::size_t from) const;
    // `Point::Point(` and `Outer::Inner::~Inner(` have no type before the
    // name and the name IS a type, so the specifier list has to decline them.
    bool atUntypedMemberDefinition() const;
    const Type *currentClass_ = nullptr;
    // Set while a namespace- or block-scope typedef's decl-specifiers are read (1.82): a specialization put off
    // is left so until the typedef is read. typedefDefer_ is the typedef's own template-id alone, cleared for
    // what its arguments make.
    bool typedefSpecifiers_ = false;
    bool typedefDefer_ = false;
    // **The class a friend's body was written in** ([class.friend]/7, [basic.lookup.unqual]/9): its static
    // members are found by their bare names there, as in a member's body, but there is no `this` (1.82).
    const Type *friendScope_ = nullptr;
    int thisOffset_ = 0;
    const Type *enumSpecifier();
    bool atDeclarationStart() const;
    // [dcl.typedef]/9: whether the decl-specifiers in [from, to) define an unnamed class, the one case a typedef
    // gives a class its name for linkage. `typedef T type;` in a template over an unnamed union does not: 1.82,
    // it did, the union's name came to hold a specialization over itself, and std::swap of one never ended.
    bool typedefDefinesClass(std::size_t from, std::size_t to) const;
    // Where a written qualified type name ends, **past a template argument list**.
    std::size_t qualifiedTypeEndPastArgs() const;
    bool sawVolatile();
    bool cvSpecifier(Qualifiers *quals);     // a written `const` or `volatile` among the specifiers, refused when it is the second

    const Type *specifiers(StorageClass *storage, Qualifiers *quals = nullptr);
    const Type *unqualifiedSpecifiers(StorageClass *storage, Qualifiers *quals);

    Declared declarator(const Type *base, bool nameOptional = false,
                        bool insideParens = false);
    // `operator+` where a declarator wants a name: the whole of it is the name, so every
    // table keys it as it keys `get`. Then a lambda - rung 7.6 - and `P(1)`, a class
    // temporary built into a slot of this frame.
    ExprPtr classTemporary(const Type *cls, std::size_t pos);
    // The temporary `T(args)` makes, its arguments read already.
    ExprPtr classTemporaryFrom(const Type *cls, std::vector<ExprPtr> args, std::size_t pos);
    // **[class.copy]/31: the temporary that would be copied into an object of its own class is that object.**
    // The frame slot of the temporary a prvalue `T(args)` is - as classTemporary and constructTemporary build
    // it, not one an arm of `?:` or a call made - or 0. Who takes the slot for the object it initialises
    // calls no copy or move constructor and no destructor of a temporary (1.78).
    int freshTemporarySlot(const Expr &e, const Type *cls) const;
    // The temporary in that slot is no longer the full-expression's to destroy.
    void adoptTemporary(int slot);
    // The constructor's call inside a constructed temporary, `*((call[, guard = 1]), &$tmp)`, whose first
    // argument is the temporary's address - or null where the temporary was made another way. Given another
    // address there, the same expression builds the object at that address.
    Call *temporaryConstruction(const Expr &e) const;
    // [expr.static.cast]/4: a cast of an object to its own class, or to a base of it, by value - a copy.
    ExprPtr castToOwnClass(ExprPtr &v, const Type *to, std::size_t pos);
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
    // The const locals with known values of the function a lambda was written in, by closure tag:
    // its body reads them without a capture, [expr.prim.lambda]/12 (1.27).
    std::map<std::string, std::vector<Local> > closureConstants_;
    std::vector<Local> constantLocals() const;
    // Whether class scope declares the name, so that it hides a global of that name.
    bool classScopeDeclares(const std::string &name);
    bool hasOrdinaryMember(const Type *cls, const std::string &name);
    // Does this type mention a template parameter - the one test the pattern read's stand-ins ask.
    bool dependentType(const Type *t) const;
    bool dependentArgs(const std::vector<TemplateArg> &args) const;
    // **Expression SFINAE**: an expression over a pattern's parameters, read as a tree that types
    // nothing (ParserDependent.cpp). `angle` stops at an argument list's `>`; `comma` lets `,` in.
    bool dependentAhead(bool angle) const;
    const DepExpr *dependentExpression(bool angle, bool comma);
    const DepExpr *depAssignment(bool angle);
    const DepExpr *depBinary(int least, bool angle);
    const DepExpr *depUnary(bool angle);
    const DepExpr *depPostfix(bool angle);
    const DepExpr *depPrimary(bool angle);
    const DepExpr *depCast(const Type *to);
    const DepExpr *depNode(DepExpr e);
    void depRefuse(std::size_t pos, const std::string &what);
    std::string depText(std::size_t from, std::size_t to) const;
    // The function an expression designates as an lvalue, or null.
    const Type *functionDesignated(const Expr &e) const;
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
    // Where `base` sits in `derived` along a path accessible here, [class.access.base]/4; -1 if none.
    int accessibleBaseOffset(const Type *derived, const Type *base) const;
    // Whether code here may step from `d` to a base it names with access `how`.
    bool baseStepAccessible(const Type *d, Access how) const;
    // [class.access.base]/5: a member declared in `decl` with `own` access, named in `n`.
    bool memberReachable(const Type *n, const Type *decl, Access own) const;
    // A name that is a *capture of the lambda around this one*.
    ExprPtr outerCaptureAccess(const std::string &name);
    // **One closure type per lambda written, however often it is read.** 7.1 reads an
    // `auto` initialiser twice, so a lambda reached here twice and made two classes, and
    // the declaration then refused its own initialiser. Keyed by the '[' token.
    struct MadeLambda {
        const Type *type;
        std::size_t end;
        // How many tokens the lambda spans, for a reading of a copy of them.
        std::size_t span = 0;
        // The captures have to be copied in on *every* reading, not only the
        // one that built the class - the second reading was handing back an
        // uninitialised closure and the lambda saw whatever was on the stack.
        std::vector<std::string> names;
        std::vector<const Type *> types;
        std::vector<int> offsets;
    };
    // One closure object: a frame slot, and each capture copied into it.
    ExprPtr buildClosure(const MadeLambda &made, std::size_t pos);
    // A closure met while an enclosing lambda's body was read for its return type (A21),
    // by that reading's function and the `[`'s source position: the real reading, from a
    // copy of the tokens, takes it back, so the deduced return type and the object agree.
    typedef std::map<std::pair<std::string, std::size_t>, MadeLambda> ClosureMap;
    ClosureMap deducedAt_;
    // **Keyed by the function being compiled as well**: a template's body is replayed for every instantiation, and one closure shared by `tw<int>` and `tw<double>` ran int's body for both.
    ClosureMap lambdaAt_;

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
    // Whether that function is inline - written so, defined in a class, or a template's - so that a
    // static local of it is one object for the program, in a COMDAT under the ABI's name (1.28).
    bool currentFunctionInline_ = false;
    int tssSeq_ = 0;  // cl's `$TSS0`, `$TSS1` guards, counted per function

    bool isFriendOf(const Type *cls) const;

    // [class.local]. A class defined in a function body belongs to that function: two
    // functions may each define `struct L` and they are different types. Both facts come
    // from these tables - a qualified tag, and a scope emptied when the function ends.
    std::map<std::string, const Type *> localTypes_;   // written name -> type
    // The empty statement a declaration that makes no code becomes, marked as one.
    static StmtPtr declarativeStatement() {
        Block *b = new Block({});
        b->setDeclarative();
        return StmtPtr(b);
    }
    // A block's typedefs, [basic.scope.block]: each entry the name and what it hid (null: nothing), undone at
    // the block's '}'; the entries from localTypeBlockStart_ on are the open block's own.
    std::vector<std::pair<std::string, const Type *> > localTypeUndo_;
    std::size_t localTypeBlockStart_ = 0;
    std::map<std::string, std::string> localClassOwner_;  // tag -> enclosing symbol
    // tag -> the class whose member function the local class is written in, which it shares the
    // access of ([class.access]/2).
    std::map<std::string, const Type *> localClassScope_;
    // The enclosing function's linkage name, for a class that is local to one.
    const std::string *localOwnerOf(const std::string &tag) const;
    // An operator this compiler can *name* but cannot yet reach from an expression is
    // refused where it is declared. The arity decides which - `operator-` is two
    // functions and only one is missing - so it is asked once the parameters are known.
    void checkOperatorDeclarable(const std::string &name,
                                 const std::vector<const Type *> &params,
                                 bool member, std::size_t pos,
                                 bool internal = false, const Type *returns = nullptr);
    // [over.literal]/2-/8: where a literal operator may be declared, and the lists it may take.
    void checkLiteralOperator(const std::string &name, const std::vector<const Type *> &params,
                              bool member, std::size_t pos);
    // [over.literal]/5: a literal operator template's own list is one `char...` and its function's empty.
    void checkLiteralOperatorTemplate(const TemplateDecl &decl, const Type *pattern);
    // [lex.ext]/2-8: a literal with a ud-suffix, as the call of the literal operator it names.
    ExprPtr userDefinedLiteral(Program *program);
    // Whether the literal at the cursor - adjacent strings taken together - carries a ud-suffix.
    bool atUserDefinedLiteral() const;
    const Type *arraySuffix(const Type *base, std::size_t pos);
    const Type *promote(const Type *t) const;
    const Type *usualArithmetic(const Type *a, const Type *b) const;
    ExprPtr convert(ExprPtr e, const Type *to, bool allowExplicit = false) const;
    // The base subobject of a glvalue of a derived class, `*(B *)&d` - a base's offset carried.
    ExprPtr baseSubobject(ExprPtr e, const Type *base) const;
    // [conv.mem]: a pointer to a member of B becomes one to a member of D, D
    // derived from B; the base's offset, or -1 where that is not the case.
    int memberPointerToDerived(const Type *from, const Type *to) const;
    ExprPtr convertMemberPointer(ExprPtr e, const Type *to, int shift) const;
    const Type *unsignedVersion(const Type *t) const;

    ExprPtr decay(ExprPtr e);
    void requireScalar(const Expr &e, std::size_t pos, const char *what);

    void checkAssignable(const Expr &from, const Type *to, std::size_t pos,
                         const std::string &what) const;

    int declare(const std::string &name, const Type *type, std::size_t pos,
                int alignAtLeast = 0);
    int allocateFrameSlot(const Type *type, int alignAtLeast = 0);
    // A synthesised function's own frame, opened mid-function: size 0 and no free slot, the caller's put back by closeFrame.
    struct FrameSave { int size; std::vector<FreeSlot> free; };
    FrameSave openFrame();
    void closeFrame(const FrameSave &saved);
    int alignasSpecifier();
    void refuseWeakAlignas(int asked, const Type *t, std::size_t pos);
    void declareStaticLocal(const std::string &name, const Type *type,
                            std::size_t pos, const std::string &symbol);
    void requireAssignable(const Expr &e, std::size_t pos, const char *what);
    const Local *findLocal(const std::string &name) const;

    // The construct being read, kept where a failure leaves it: back to the one around it on any other way out.
    bool readingProgram() const { return instantiationDepth_ == 0 && classesInstantiating_ == 0 && !src_.inTrial(); }
    struct ReadingAt {
        Parser *p; std::size_t *slot, was;
        ReadingAt(Parser *parser, std::size_t *at) : p(parser), slot(at), was(*at) { if (p->readingProgram()) *at = p->peek().pos; }
        ~ReadingAt() { if (!p->src_.failed()) *slot = was; }
    };
    std::size_t stmtAt_ = npos(), memberAt_ = npos(), declAt_ = npos();
    // One level of the instantiation chain said under an error.
    struct InChain {
        const Source &s;
        InChain(const Source &src, std::size_t pos, const std::string &name) : s(src) {
            s.enterInstantiation(pos, "in the instantiation of '" + name + "' asked for here");
        }
        ~InChain() { s.leaveInstantiation(); }
    };
    // A specialization read from the queue: under the chain that asked for it, and the one under way again after.
    struct ChainOf {
        const Source &s;
        std::vector<Source::Context> was;
        ChainOf(const Source &src, const std::vector<Source::Context> &chain) : s(src), was(src.instantiations()) { s.setInstantiations(chain); }
        ~ChainOf() { s.setInstantiations(std::move(was)); }
    };
    // The first mention of a local declared with no initialiser, read or written; see noteUnsetLocal.
    void noteUnsetLocal(const Local &l, std::size_t pos);
    void noteUnsetArguments(const std::vector<ExprPtr> &args, const std::vector<const Type *> &params);
    void forgetUnsetArguments();
    std::map<std::string, std::size_t> unsetArguments_;
    void warnAssignmentAsCondition();
    // **A template's own scope has none of the naming function's locals**:
    // held aside while it is read and put back on every way out, with the
    // function's packs and freed frame slots (reused, one overlaid a param).
    struct LocalsHidden {
        Parser *p;
        std::vector<Local> held;
        std::map<std::string, PackBinding> packs;
        std::vector<FreeSlot> slots;
        explicit LocalsHidden(Parser *q) : p(q), packs(q->packs_) { held.swap(p->locals_); slots.swap(p->freeSlots_); }
        ~LocalsHidden() { held.swap(p->locals_); p->packs_ = packs; slots.swap(p->freeSlots_); }
    };
    bool localHidesType() const;
    std::size_t temporaryCallEnd(std::size_t *open = nullptr) const;
    bool atTemporaryMemberAccess() const;
    bool atTemporaryOfArguments() const;
    void enterScope();
    void leaveScope();

    int enterBlock();
    void leaveBlock();
    int currentBlock() const { return blockStack_.empty() ? 0 : blockStack_.back(); }
    const GlobalSym *findGlobal(const std::string &name) const;
    GlobalSym *findGlobalToUpdate(const std::string &name);

    const Type *composite(const Type *a, const Type *b);
    const Type *compositePointer(const Type *a, const Type *b);
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
    bool tagRulesOut(const Type *param, const Type *given);
    // **What a trait's trial made and what those bodies called**: the trial's marks are put back, and a
    // member template's body is read once, so a real use of what it made has to ask for them again.
    struct TrialUses {
        std::size_t from = 0, to = 0;
        std::vector<std::size_t> called;
        // The member templates' specializations the trial made: theirs are the bodies read once, so a real
        // use of one of these - and of nothing else made in the trial - asks for `called` again (1.77).
        std::vector<std::size_t> made;
    };
    std::vector<TrialUses> trialUses_;
    bool carryTrialUses();
    // The functions' `used` marks kept, and put back with what was marked in between remembered for the
    // specializations made in between (TrialUses). `usesHeld_` counts the holders, so that only the outermost keeps.
    void holdUses(std::vector<char> &used);
    void releaseUses(const std::vector<char> &used);
    int usesHeld_ = 0;
    // `object` is the type the call is made on for a member function and null for a free
    // one: **the implicit object parameter is ranked like any other**, which is what
    // tells `get()` from `get() const`. **By value** - `functions_` can move under you.
    void instantiateAdlTemplates(const std::string &written, const std::vector<std::string> &spaces,
                                 const std::vector<ExprPtr> &args, std::size_t pos);
    Signature resolveOverload(const std::string &name,
                                     const std::vector<ExprPtr> &args,
                                     std::size_t pos,
                                     const Type *object = nullptr,
                                     bool objectLvalue = true);
    // `&` or `&&` after a member's parameter list - [dcl.fct]/6 - consumed; None where there is none.
    RefQual refQualifier();
    // [over.match.funcs]/4: whether a member with this ref-qualifier and const takes the object.
    static bool refQualifierViable(RefQual ref, bool constThis, bool objectLvalue);
    static std::string refQualifierRefusal(const std::string &name, bool objectLvalue);
    static std::string refQualifierMixRefusal(RefQual written, RefQual other);
    void refuseMemberPointerCategory(const Expr &mp, bool lvalue, std::size_t pos) const;
    void refuseSpecialRefQualifier(const char *what) const;

    ExprPtr newExpression(std::size_t pos);
    ExprPtr deleteExpression(std::size_t pos);
    // `p->~T()`, `o.~T()`, `p->B::~B()` and the pseudo-destructor on a scalar; null, nothing read, when the tokens are not that.
    ExprPtr explicitDestructorCall(ExprPtr &object, std::size_t pos);
    // The virtual destructor read out of the object held in `temp`: Itanium's D1 or D0 slot, Microsoft's `??_G` with the flag.
    ExprPtr virtualDestructorCall(const std::string &temp, int slot, const Type *t,
                                  const Type *cls, bool deleting, std::size_t pos);
    // `try { ... } catch (T e) { ... }` - rung 6.3.
    // A constructor's function try block hands its body in as `bodyFn` - parsed and assembled with the
    // `try` open - and `rethrows`: each handler ends rethrowing, [except.handle]/15.
    StmtPtr tryStatement(std::size_t pos, const std::function<StmtPtr()> &bodyFn = nullptr,
                         bool rethrows = false, const std::string &spec = std::string(),
                         const std::vector<const Type *> *allowed = nullptr);
    // Microsoft: a handler per type a `throw(T...)` list allows, each rethrowing, and `last` as the catch-all.
    std::vector<MsHandler> msSpecHandlers(const std::vector<const Type *> &allowed, StmtPtr last,
                                          std::size_t pos);
    // Inside a handler of a constructor's function try block, where `return` is ill-formed.
    bool inCtorTryHandler_ = false;
    // Set while a try's body is read, so a try inside one is refused.
    bool inTryBody_ = false;
    // The last local serial before the innermost try body read now began - [class.copy]/31's
    // "scope does not extend beyond the innermost enclosing try-block" - or -1 outside one.
    int tryBodySerial_ = -1;
    // [class.copy]/31-32: the operand of `throw` is moved from, not copied, where it may be.
    bool throwOperandMoves(const Expr &value) const;
    // static_cast and reinterpret_cast refuse a const the target pointer would drop.
    void refuseCastingAwayConst(const Type *from, const Type *to, std::size_t pos, const char *cast) const;
    // Inside a Microsoft `catch` body, which is compiled as a funclet - a function of its own.
    bool inMsHandler_ = false;
    MsExit *msExit_ = nullptr;
    int msUnwindHelp();          // a fresh slot; the walker keeps one per function
    std::string msExitLabel(std::string &label, const char *kind);
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
    // How many regions have asked where to go: a handler whose body cannot throw still defines its pad
    // when a local's cleanup row inside it named the pad.
    mutable int unwindTargetsAsked_ = 0;
    // Where an exception leaving this point goes.
    std::string unwindTarget(int *ptrSlot, int *selSlot) const;
    // One temporary of a block, released under its guard by a pad.
    void releaseGuarded(std::vector<StmtPtr> &steps, const Temporary &t);
    ExprPtr temporaryAddress(const Temporary &t);
    // `body` under a cleanup of its own: `onUnwind` runs when an exception leaves it, and the unwinding goes on.
    StmtPtr cleanupRegion(std::vector<StmtPtr> body, StmtPtr onUnwind);
    // Where each open handler's block begins, telling a `goto` from a jump.
    std::vector<std::size_t> handlerFrom_;
    // Where each open `try` block begins, for the jumps [except.handle]/2 forbids into one.
    std::vector<std::size_t> tryFrom_;
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
    // The same for a handler: how much was alive when its block was entered. A cleanup inside the handler hands
    // over to the handler's own pad ($endcatch) and does not resume, so no enclosing cleanup of the handler runs
    // after it - its pad destroys everything the handler's block built so far, not only its own block's. Until
    // 1.69 it destroyed its own block's alone: `catch (int e) { T t(e); { T u(e); throw; } }` left t standing
    // on the Itanium targets and the C6000 ([except.ctor]/1; the suite's eh05 and eh09).
    std::size_t handlerAliveFrom_ = 0;
    std::vector<Try *> tryBodySegments_;
    // 1-based, first occurrence winning, matching Walker::lsdaTable byte for byte.
    int typeIndexFor(const std::string &symbol);
    // Set where a function writes a `try`.
    bool functionHasTry_ = false;
    // `throw x;` - rung 6.2. Answers the statement it lowers to.
    StmtPtr throwStatement(ExprPtr value, std::size_t pos);
    void checkThrowOperand(const Expr &value, std::size_t pos);
    StmtPtr microsoftThrow(ExprPtr value, std::size_t pos);
    // `throw;` - the runtime rethrows what is being handled: __cxa_rethrow, or _CxxThrowException with
    // two null pointers, as cl calls it from a funclet.
    ExprPtr rethrowCall();
    ExprPtr terminateCall();
    // The copy constructors and destructor a Microsoft ThrowInfo chain names, marked used, and the chain recorded.
    void finishMicrosoftThrow(MicrosoftThrow &names, bool thrown);
    // A call to something in the runtime, named by its symbol and needing no
    // declaration - the same shape callAllocator has used for operator new.
    // A temporary built by a named constructor from arguments already parsed.
    ExprPtr constructTemporary(const Type *plain, const Signature &ctor,
                               std::vector<ExprPtr> args, bool zeroFirst,
                               std::size_t pos);
    // **[over.ics.user]**.
    const Signature *convertingConstructor(const Type *to, const Expr &from,
                                           std::size_t pos = 0, bool directInit = false);
    // The conversion function on `from` giving `to`, or - with `to` null - any
    // scalar, `bool` first. The mirror of the constructor above.
    const Signature *conversionFunction(const Type *from, const Type *to,
                                        bool allowExplicit = false);
    // The conversion function a reference to `referent` is bound through, [dcl.init.ref]/5 (ParserExprNew.cpp).
    const Signature *referenceConversion(const Type *from, const Type *referent);
    // A class where a number or a pointer is wanted, converted by its own.
    ExprPtr contextualScalar(ExprPtr e, std::size_t pos, const char *what);
    // The single conversion to a scalar, or null where there are none or two.
    const Signature *soleNumericConversion(const Type *from);
    // **Only one user-defined conversion may appear in a sequence**.
    int rankingConversion_ = 0;
    // The arguments being ranked, by rank position - the object first where there is one, the
    // operands for an operator - so that closerTarget knows what the parameters took (1.27).
    std::vector<const Type *> rankingGiven_;
    // The conversion itself, or null when none is needed or possible.
    ExprPtr userConversion(const Type *param, ExprPtr &arg, std::size_t pos);
    const Signature *conversionOverConstructor(const Type *plain, const Expr &arg, const Signature &ctor,
                                               std::size_t pos, bool *compared = nullptr);
    void copyFromOtherClass(const Type *cls, std::vector<ExprPtr> &args, std::size_t pos);
    // **[over.ics.user] through a conversion *function***: one argument of
    // another class that converts to `cls`. An explicit one counts only in
    // direct-initialisation, [over.match.copy]/1.
    bool convertThroughConversionFunction(std::vector<ExprPtr> &args,
                                          const Type *cls, bool directInit,
                                          std::size_t pos);
    bool constructorViable(const Type *cls, const std::vector<ExprPtr> &args,
                           std::size_t pos = 0);
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
    const Signature *classAllocator(const Type *made, const char *which);
    ExprPtr typeidExpression(std::size_t pos);
    ExprPtr deallocate(const Type *pointee, ExprPtr raw, std::size_t pos);
    ExprPtr deallocateArray(const Type *pointee, ExprPtr raw, std::size_t pos);
    // `new (a, b) T`: the `operator new` the placement arguments reach, class first.
    ExprPtr userPlacementAllocation(const Type *made, bool array, ExprPtr bytes,
                                    std::vector<ExprPtr> &extra, std::size_t pos);
    ExprPtr callNothrowDeallocator(bool array, ExprPtr raw);
    ExprPtr callNothrowAllocator(bool array, ExprPtr bytes, ExprPtr tag, std::size_t pos);
    ExprPtr callAllocator(const char *itanium, const char *microsoft,
                          const Type *returns, ExprPtr arg, std::size_t pos);
    int newTemps_ = 0;
    // The class a member template's signature is read in, to its end: inlineOwner_ is the declarator's one-shot
    // and is gone by the parameters, which name member types too - `put(const_iterator, It)`.
    std::string signatureOwner_;
    // **[over.match.copy]/1 and [over.match.ctor]/1: copy-initialisation's candidates are the converting
    // constructors.** Set around the resolveOverload that chooses one; that call takes it and clears it, so
    // what it instantiates resolves as usual.
    bool convertingOnly_ = false;
    struct ConvertingOnly {
        Parser &p;
        bool was;
        ConvertingOnly(Parser &q, bool on) : p(q), was(q.convertingOnly_) { p.convertingOnly_ = on; }
        ~ConvertingOnly() { p.convertingOnly_ = was; }
    };

    void parseArguments(std::vector<ExprPtr> &args);
    // The copy the caller makes for a by-value class argument: a temporary in the
    // caller's frame, built by the copy constructor, whose address the callee receives.
    // And `p != 0 ? (body, 1) : 0`, which [expr.delete]/2 wraps a destructor call in.
    ExprPtr guardAgainstNull(const std::string &temp, int slot,
                             const Type *ptr, ExprPtr body);
    ExprPtr materialiseCopy(const Type *type, ExprPtr arg, std::size_t pos,
                            const std::string &what,
                            std::vector<Temporary> &destroy);
    ExprPtr copyInitialise(const Type *param, ExprPtr arg, std::size_t pos, const std::string &what,
                           std::vector<Temporary> &destroy);
    // ---- The type-trait built-ins `<type_traits>` is written with (ParserTraits.cpp) ----
    // `__is_enum(T)`, `__is_constructible(T, Args...)` and the rest clang and cl share, a bool constant each.
    static bool isTypeTraitName(const std::string &word);
    ExprPtr typeTrait();
    const Type *underlyingTypeSpecifier();
    std::vector<const Type *> traitOperands();
    void traitNeedsComplete(const Type *t, const std::string &name, std::size_t pos);
    bool typeTraitHolds(int trait, const std::vector<const Type *> &ops, std::size_t pos);
    // [meta.unary.prop]'s create<T>(): what the token `$trait<n>` reads as in the expression a trait builds.
    ExprPtr traitOperand(const Type *t);
    std::vector<const Type *> traitOperands_;
    enum TraitForm { kTraitConstruct, kTraitCast, kTraitAssign, kTraitConvert };
    bool traitWellFormed(TraitForm form, const Type *to, const std::vector<const Type *> &operands,
                         std::size_t pos, bool *nothrow);
    bool traitConstructible(const Type *t, const std::vector<const Type *> &args, std::size_t pos,
                            bool *nothrow, bool *trivial);
    bool traitDestructible(const Type *t) const;
    enum SpecialMember { kDefaultCtor, kCopyCtor, kMoveCtor, kCopyAssign, kMoveAssign, kDestructor };
    std::vector<const Signature *> specialMembers(const Type *cls, SpecialMember which) const;
    bool implicitMove(const Type *cls) const;
    bool trivialSpecial(const Type *t, SpecialMember which) const;
    bool hasTrivialDefaultConstructor(const Type *cls) const;
    bool virtualDestructor(const Type *cls) const;
    bool triviallyCopyable(const Type *t) const;
    bool standardLayout(const Type *t) const;
    bool implicitDefaultDeleted(const Type *cls) const;
    bool implicitDefaultIsConstexpr(const Type *cls) const;
    bool literalType(const Type *t) const;
    bool hasConstexprConstructor(const Type *c) const;
    bool plainlyNotLiteral(const Type *t) const;
    bool emptyClass(const Type *t) const;
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
    // The call an expression of a class is, past the commas before it - a virtual call's shape - or null.
    static Call *resultCall(const Expr *e);
    ExprPtr endFullExpression(ExprPtr e);
    // Drop the temporary this expression yields from the pending list, because its ownership is leaving the frame.
    bool releaseTemporary(const Expr &value);
    // [class.temporary]/5 - a temporary bound to a reference outlives its full
    // expression and is destroyed with the reference's scope instead.
    bool extendTemporary(const Expr &addr, const std::string &name);
    // The index in pendingTemps_ of the temporary a bound address names, or -1.
    int pendingTemporaryOf(const Expr &e) const;
    void flushTemporaries(std::vector<StmtPtr> &into, bool inBlock = false);
    void flushOverObject(std::vector<StmtPtr> &into, const Alive &made, std::size_t pos);
    bool mainDefinedHere() const;
    bool microsoftEntry(const std::string &key) const;
    void refuseInaccessibleDestructor(const Type *t, std::size_t pos, const std::string &what) const;
    // Uses of `main` met before its definition: refused at the end of the unit if it defines it.
    std::vector<std::size_t> mainUses_;
    ExprPtr completeCall(const std::string &name, const std::string &symbol,
                         ExprPtr callee, const Type *returns,
                         const std::vector<const Type *> &params, bool variadic,
                         std::size_t pos, std::vector<ExprPtr> args,
                        bool hasThis = false, int vbInit = 1);

    void parameterTypes(std::vector<const Type *> &params, bool &variadic);
    void checkDefaultArgument(std::size_t from, std::size_t to, const std::vector<std::string> &params,
                              const Type *cls, bool locals);
    struct MemberDefault { const Type *cls; std::size_t from, to; };
    std::vector<MemberDefault> memberDefaults_;
    void checkMemberDefaults(const Type *type);
    // **Microsoft mangles a parameter from before [dcl.fct]/5 drops its cv.**
    const Type *parameterAsWritten(const Type *declared, const Type *adjusted);
    // `fn` respelled for the Microsoft mangler where this function's
    // parameters were written differently from their types, and `fn` itself
    // otherwise.
    const Type *manglingType(const Type *fn);
    std::vector<const Type *> writtenParams_;
    std::vector<const Type *> writtenFor_;
    // [dcl.fct]/5: the parameter list after a parameter's name, read as that parameter's function
    // type and adjusted to a pointer to it - the list it is in left as it was.
    const Type *functionTypeParameter(const Type *returns);
    Declared parameterDeclarator(const Type *base);
    // An exception specification where a function type is written - not part of it in C++11.
    // Answers 0 where none is written, 1 for a non-throwing one, 2 for a list that lets some through.
    int skipExceptionSpecification();
    // The one the pointer-to-function declarator just read wrote, which [except.spec]/5 asks of the
    // function it is initialised with; 0 once asked, or where none was written.
    mutable int fnPointerSpec_ = 0;
    // The pointers to function declared `noexcept`: at namespace scope by name, a data member as
    // `class::name`; a local's is on its Local.
    std::set<std::string> noexceptPointers_;
    // A call through a pointer so declared promises what the pointer did.
    bool callsNoexceptPointer(const Expr &callee) const;
    // [except.spec]/14: whether a destructor may throw - its own specification, or with none those of
    // the bases and members it destroys.
    bool destructorMayThrow(const Type *cls) const;
    // The objects from `from` on in the block whose `blockFrom` is given that a destructor's throw would
    // find already destroyed, each put under a guard.
    void guardThrowingDestructors(std::vector<StmtPtr> &body, std::size_t blockFrom, std::size_t from);
    StmtPtr destroySubobjects(StmtPtr body, const Type *type, int thisSlot, std::size_t pos);
    // The destructor being read: the label after its body, and whether a `return` went to it.
    std::string dtorEndLabel_;
    bool dtorReturned_ = false;
    StmtPtr leaveBody();

    // **A default argument is kept as a place in the token stream, not as a parsed
    // expression**: [dcl.fct.default] evaluates it afresh at every call that leaves it
    // out. `pendingDefaults_` carries them to declare(); `defaultArgs_` keys them.
    std::vector<std::size_t> pendingDefaults_;
    std::map<std::string, std::vector<std::size_t> > defaultArgs_;
    // **A function template's specialization reads its defaults with its own arguments bound** -
    // `T b = T()` names T, which the call site does not have (R17: "'T' was not declared").
    struct DefaultBinding {
        std::vector<TemplateParam> params;
        std::vector<const Type *> binding;
        std::vector<long long> values;
        Packs packs;
    };
    std::map<std::string, DefaultBinding> defaultArgBindings_;
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
    // A function a block's using-declaration names, by its bare name: [basic.lookup.unqual] finds it before
    // the class's members - `using std::swap; swap(c, q.c);` inside queue::swap.
    std::map<std::string, std::string> blockUsingFunctions_;
    // `namespace A = N;` - the alias's qualified name to the namespace it names, for the
    // readers that meet the alias without a `::` after it; every other use is rewritten.
    std::map<std::string, std::string> namespaceAliases_;
    void namespaceAlias(const std::string &name, std::size_t pos, bool inBlock = false);
    // What a block's namespace aliases and using-declarations of functions put in these two tables:
    // each key and what it named before the block, empty for nothing; undone at its '}'.
    struct BlockUndo {
        std::map<std::string, std::string> *table;
        std::string key;
        std::string was;
    };
    std::vector<BlockUndo> blockUndo_;
    void enterBlockEntry(std::map<std::string, std::string> &table, const std::string &key,
                         const std::string &value);
    void blockUsingDeclaration();
    // WS-D: `auto f(params) -> T`, the return type read after the parameters.
    void refuseTrailingBase(const Type *declared, std::size_t pos);
    const Type *readTrailingReturn(std::size_t paramsOpen);
    void declareParametersAt(std::size_t paramsOpen);
    // **CWG 1330: a member function's exception-specification is read with the class complete**, so its
    // `noexcept(e)` may name a member declared below it - `void swap(P &p) noexcept(noexcept(swap(first,
    // p.first)));` with `first` after. It is tried where it is written; one that fails there is kept - where its
    // operand starts and where the parameter list opens - and read again when the class closes (1.80).
    struct DeferredSpec {
        const Type *cls = nullptr;
        std::size_t function = 0;      // index into functions_
        std::size_t operandAt = 0;
        std::size_t paramsOpen = 0;
    };
    std::vector<DeferredSpec> deferredSpecs_;
    bool specDeferred_ = false;
    std::size_t specOperandAt_ = 0, specParamsOpen_ = 0;
    void keepDeferredSpec();
    void settleDeferredSpecs(const Type *cls);
    bool noexceptOperandMayNameParameters(std::size_t from) const;
    std::size_t skipFunctionQualifiers(std::size_t i) const;
    bool autoBeforeQualifiedName() const;
    bool trailingArrowAhead() const;
    StmtPtr delegatingCall(const Declared &d, const Type *cls, std::vector<ExprPtr> args,
                           std::size_t pos, std::size_t entries);
    std::string resolveNamespaceName(const std::string &written) const;
    // **An unnamed namespace is a named one that nothing outside can name.**
    bool inUnnamedNamespace_ = false;
    // **The body being read belongs to a static member.**
    bool inStaticMember_ = false;
    // A static member's definition is read in its class's scope, as a static
    // member function's body is - [class.static.data]/2; put back on the way out.
    struct StaticMemberScope {
        Parser *p; const Type *cls; bool wasStatic;
        StaticMemberScope(Parser *parser, const Type *owner)
            : p(parser), cls(parser->currentClass_), wasStatic(parser->inStaticMember_) {
            if (owner != nullptr) { p->currentClass_ = owner; p->inStaticMember_ = true; }
        }
        ~StaticMemberScope() { p->currentClass_ = cls; p->inStaticMember_ = wasStatic; }
    };
    // While a thread_local is declared: its construction, binding and destructor go to __tls_init.
    struct ThreadLocalObject {
        Parser *p; bool was;
        ThreadLocalObject(Parser *parser, bool tls) : p(parser), was(parser->tlsObject_) { p->tlsObject_ = tls; }
        ~ThreadLocalObject() { p->tlsObject_ = was; }
    };
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
    // Every namespace defined `inline`, by full path - [namespace.def]/8.
    std::set<std::string> inlineNamespaces_;
    // Unnamed classes named so far after their first declarator, for clang's `$_N`.
    int unnamedTypes_ = 0;
    // Every prefix `a::b::` becomes with inline components taken out: `lib::v2::`'s member is `lib::`'s.
    std::vector<std::string> inlineAliasPrefixes(const std::string &prefix) const;
    // The four tables' sizes before a declaration, so the entries it added can be aliased.
    struct TableSizes { std::size_t functions, globals, typedefs, enums; };
    TableSizes tableSizes() const {
        return TableSizes{ functions_.size(), globals_.size(), typedefs_.size(), enums_.size() };
    }
    void aliasInlineMembers(const TableSizes &before);
    void aliasTypeKey(const std::string &key, std::size_t index);
    void aliasFunctionKey(const std::string &key, std::size_t index);
    // [namespace.udir]/6: a name found in the namespace and in an inline one of it, as two
    // entities, is ambiguous - refused where the key is turned into the entity it names.
    void refuseInlineAmbiguity(const std::string &key, std::size_t index,
                               const std::unordered_map<std::string, std::size_t> &table,
                               bool (Parser::*same)(std::size_t, std::size_t) const = nullptr) const;
    // Two typedef entries naming one type are one entity, as a typedef written twice is.
    bool sameTypedef(std::size_t a, std::size_t b) const { return typedefs_[a].type == typedefs_[b].type; }
    // The namespace a class was declared in, as namespaceStack_ spells it (n::m as {n, m}).
    std::vector<std::string> classNamespacePath(const Type *t) const;
    // The namespace an out-of-line definition was written for, while it is read; its defaults read there.
    std::vector<std::string> definitionNamespace_;
    // `int lib::f(int) {}` reaches an f declared in an inline namespace of lib; an unqualified one declares anew.
    bool definingQualified_ = false;
    // The qualified name to look a written one up under: the enclosing namespaces from
    // the innermost outwards, then whatever a `using namespace` has opened, then the
    // name itself. `exists` says which table to ask, functions and variables differing.
    std::string qualifyForLookup(const std::string &name,
                                 bool (Parser::*exists)(const std::string &) const) const;
    // The namespace a first component names, innermost out: `inner` in `outer` is "outer::inner".
    std::string namespaceNamed(const std::string &first) const;
    static std::vector<std::string> namespacePathStack(const std::string &ns);
    // How many tokens from `from` spell a namespace qualifier, `lib :: v2 ::` being four; 0 for none.
    std::size_t namespaceQualifierTokens(std::size_t from) const;
    // An unqualified name declared in an enclosing namespace, innermost first, or "" -
    // asked before the flat table, since [basic.lookup.unqual]/1 lets the nearer one hide a global.
    // A type named in the classes this is inside, innermost first, or null.
    const Type *classScopeType(const std::string &name) const;
    std::string enclosingNamespaceKey(const std::string &name,
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
    // The type the chain qualifiedTypeEnd measured reaches, a member typedef included.
    const Type *qualifiedTypeAt(std::size_t end) const;
    std::vector<std::string> lookupKeys(const std::string &name,
                                        const Type *left,
                                        const Type *right) const;
    // [temp.res]/10: a function declared before pos - a specialization counts as its template, not filtered.
    static bool visibleBefore(const Signature &f, std::size_t pos) {
        return f.fromTemplate || f.pos == 0 || f.pos < pos;
    }
    std::size_t visibleAt_ = 0;
    bool hasVisibleFunctionNamed(const std::string &key) const;
    // [basic.lookup.argdep]/2: the namespaces a type brings to a call, "" the global one.
    void associatedNamespaces(const Type *t, std::vector<std::string> *out, int depth) const;
    // [temp.dep]/3: the bases of a class template's specialization written with a template parameter.
    std::map<const Type *, std::vector<const Type *> > dependentBases_;
    bool inDependentBase(const Type *cls, const Type *owner) const;

    std::map<std::string, std::size_t> memberInit_;
    // **[class.union]/5, an anonymous union.** Its members' positions, recorded as its body is read,
    // for the refusals the class around it raises; the `$S<n>` count clang numbers its objects by.
    std::map<const Type *, std::vector<std::size_t> > anonUnionMemberPos_;
    int anonUnionSeq_ = 0;
    bool anonymousUnionAhead() const;
    void refuseAnonymousUnionShape(const Type *u, std::size_t pos);
    std::string anonymousUnionSymbol(const Type *u, std::size_t pos);
    void declareAnonymousUnionObject(const Type *u, StorageClass sc, std::size_t pos, bool local);
    std::set<const Type *> unionsNamedBy(const Type *cls, const std::set<std::string> &names) const;
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
    // Where the last parameter list closed: [dcl.fct.default]/4 (defaults a
    // suffix of the list) is asked once its defaults are stored.
    std::size_t pendingDefaultsAt_ = 0;
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

    // How deep inside 'extern "C"' the parser is - zero is C++ linkage, unless someone says otherwise -
    // and whether the declaration being read is the one written straight after `extern "C"`, no
    // braces, which [dcl.link]/7 reads as if it said `extern`.
    int cLinkage_ = 0;
    bool directLinkage_ = false;
    bool linkageSpecification();
    std::string functionSymbol(const std::string &name, const Type *returns,
                               const std::vector<const Type *> &params,
                               bool variadic, bool internal, std::size_t pos);
    std::string dataSymbol(const std::string &name, const Type *type,
                           bool isStatic, std::size_t pos);

    ExprPtr objectRef(const std::string &name);
    const Type *qualifiedMemberScope(const Type *obj, std::string &name,
                                     std::size_t pos);
    // `object`, of class `obj`, as its `base` subobject - the left side of `d.B::m` once B is
    // named; *offset is set and the object kept where a constant offset reaches B.
    ExprPtr qualifiedSubobject(ExprPtr object, const Type *obj, const Type *base,
                               const Member &m, int *offset);
    void refuseAmbiguousMember(const Type *cls, const Member &m, std::size_t pos);
    // The two halves of it, because class scope sits between them.
    ExprPtr localRef(const std::string &name);
    ExprPtr globalRef(const std::string &name);
    ExprPtr useReference(ExprPtr e);
    const Type *memberTypeIn(const Type *obj, const Member &m) const;
    void refuseVolatileClassCopy(const Type *from, const Type *to, const std::string &what,
                                 std::size_t pos) const;
    void refuseDroppedQualifier(const Type *it, const Type *referent, const Type *ref,
                                const std::string &what, std::size_t pos) const;
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
    bool isAggregateClass(const Type *t) const;
    bool bracedInitialiserAhead() const;
    const Signature *listConstructorTemplate(const Type *cls, const Init &in, const Type **ilType, std::size_t pos);
    bool constructedClass(const Type *t) const;
    bool needsConstruction(const Type *t) const;
    void initConstruct(const std::string &name, std::vector<InitStep> &path, const Type *type,
                       std::vector<ExprPtr> args, bool valueInit, std::size_t pos, std::vector<StmtPtr> &out);

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
    // A12/A13: whether a file-scope initialiser folds to bytes; one that does
    // not is stored by the init function instead, once the object is declared.
    bool staticallyInitialisable(const Type *t, Init &in);
    void dynamicInitialiseScalar(const std::string &name, const Type *type,
                                 Init &in);
    void dynamicInitialiseStaticMember(const std::string &name,
                                       const std::string &symbol, const Type *type,
                                       Init &in, bool once);
    // The weak guard a template's static member is built under, so a second
    // unit's copy does not build it again; wraps `body` and returns the test.
    StmtPtr guardTemplateMember(const std::string &symbol,
                                std::vector<StmtPtr> body);
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
    // Does the initialiser ahead name `name`? Only then is it registered first ([basic.scope.pdecl]/1).
    bool initialiserNames(const std::string &name) const;

    // **A `constexpr` function, kept so that fold() can run it.** [dcl.constexpr] in
    // C++11 lets the body be one return statement, which makes evaluating a call an
    // expression fold. The function is still compiled and callable at run time.
    struct ConstexprFn {
        const Expr *value = nullptr;   // owned by the Function in the Program
        std::vector<int> slots;        // parameter frame slots, in order
        std::size_t pos = 0;
    };
    std::map<std::string, ConstexprFn> constexprFns_;   // by mangled symbol

    // **A constant object of literal class type** - [basic.types]/10, [expr.const]/2: its scalar
    // leaves by byte offset, which is all a constant expression reads of it.
    // A pointer leaf is the address of `address` plus i, or null where `address` is empty.
    struct ConstLeaf { const Type *type = nullptr; long long i = 0; long double d = 0; std::string address; };
    typedef std::map<int, ConstLeaf> ConstObject;
    typedef std::shared_ptr<ConstObject> ConstObjectPtr;
    // A `constexpr` constructor, kept as its lowered body: this slot, then the parameters'.
    struct ConstexprCtor { const Stmt *body = nullptr; std::vector<int> slots; int thisSlot = 0; };
    std::map<std::string, ConstexprCtor> constexprCtors_;   // by C1 and C2 symbol
    std::set<std::string> constexprCtorSymbols_;           // declared `constexpr` in their class
    // Named constexpr objects: a global by its name, a local by function and slot.
    std::map<std::string, ConstObject> constObjects_;
    // Named constexpr pointers into such an object or a string literal, keyed the same way: the object and the offset.
    std::map<std::string, std::pair<ConstObjectPtr, int> > constPointers_;
    bool constantPointerInit(const Init &in, const std::string &key);
    const Type *constexprObjectType(const Type *t, const Qualifiers &q);
    const Type *withoutBaseConst(const Type *t);

    // One frame per call being folded, holding what each parameter slot is worth:
    // an integer, or a double when the parameter's type is floating (A11).
    // A class parameter holds its object; a pointer to one the object and an offset in it.
    struct ConstexprSlot {
        int offset = 0;
        bool floating = false;
        long long i = 0;
        long double d = 0;
        ConstObjectPtr obj;
        bool pointer = false;
        std::string global;   // a global object being built, named by this slot
        std::string address;  // a pointer's: the symbol it points into, i the offset - beside obj or alone
    };
    mutable std::vector<std::vector<ConstexprSlot> > constexprFrames_;
    const ConstexprSlot *constexprSlot(const Var &v) const;
    // Folds the arguments and pushes a frame; false when the call cannot fold.
    bool enterConstexprCall(const Call &c, const ConstexprFn **fn,
                            std::size_t pos) const;

    const Expr *singleReturnValue(const Stmt &body) const;
    bool foldArgument(const Expr &a, ConstexprSlot *slot, std::size_t pos) const;
    bool foldObject(const Expr &e, ConstObject *out, std::size_t pos) const;
    // **An aggregate temporary `T{...}` whose list is constant**, folded where it was made and kept by the
    // expression and its temporary's name: `constexpr T3 make() { return T3{ { 1, 4, 9 } }; }` (1.82: a class value
    // made by a list did not fold).
    std::map<const Expr *, std::pair<std::string, ConstObject> > foldedAggregates_;
    static const Var *aggregateTemporaryVar(const Expr &e);
    bool foldObjectPointer(const Expr &e, ConstObjectPtr *obj, int *at, std::size_t pos) const;
    const Expr *chosenLvalue(const Expr &e, std::size_t pos) const;
    bool objectAddress(const Expr &lvalue, ConstObjectPtr *obj, int *at, std::size_t pos) const;
    bool foldLeaf(const Expr &lvalue, ConstLeaf *out, std::size_t pos) const;
    bool runConstexpr(const Stmt &s, std::size_t pos) const;
    bool runConstexprEffect(const Expr &e, std::size_t pos) const;
    bool constructConst(const Call &c, std::size_t pos) const;
    std::string constObjectKey(const Var &v) const;
    bool instantiateForConstant(const std::string &symbol) const;
    static ConstObject subObject(const ConstObject &from, int at, int size);
    static void storeSubObject(ConstObject &into, int at, int size, const ConstObject &what);
    // The object a declaration's own statements build, run with `target` standing for it.
    bool foldDeclaredObject(const std::vector<const Stmt *> &init, const Var &target, ConstObject *out,
                            std::size_t pos);
    bool constObjectFromInit(const Type *t, const Init &in, int base, ConstObject *out) const;
    void foldLocalObject(const Declared &d, int off, const std::vector<StmtPtr> &inits, std::size_t from);
    void constObjectPieces(const ConstObject &o, std::vector<GlobalPiece> &out);
    void refuseConstexprClassObject(const std::string &name, std::size_t pos) const;
    ExprPtr targetFor(const std::string &name, const std::vector<InitStep> &path);
    // Set while a member is list-initialised: targetFor's root is `this->member`, not a named object.
    const Member *initRootMember_ = nullptr;
    const Type *initRootClass_ = nullptr;
    int initRootThis_ = 0;
    // `: m{...}` or `T m = {...};` for an array or class member, [dcl.init.list]/3 at `this->m`.
    StmtPtr listInitialiseMember(const std::string &cls, const Type *type, const Member &m,
                                 int thisSlot, Init &in, std::size_t pos);

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
    // [over.match.list]/1: the initializer_list constructor is a candidate only where every element converts to E.
    bool initializerListFits(const Type *ilType, const Init &in);
    const Signature *initializerListConstructor(const Type *cls,
                                                const Type **ilType);
    ExprPtr buildInitializerList(const Type *ilType, Init &in, std::size_t pos,
                                 std::vector<StmtPtr> &into,
                                 bool fullExpression = false);
    bool constructInPlace(const Expr &value, const Type *cls, ExprPtr &at);
    int ilTemps_ = 0;
    // The symbol of a static `std::initializer_list` being initialised, whose array is static too; else empty.
    std::string staticIlFor_;

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
    // [dcl.init.string]/2: the array holds the string and its '\0', where C drops the '\0'.
    void requireStringFits(const Type *array, const StrLit *s, std::size_t pos);

    void topLevel(Program &program);
    // `[[__cxx1::when_spelled(name)]]` at the head of a declaration: true when the unit never spells the name
    // again and the declaration has been passed over.
    bool passedOverUnspelled();
    // A `{...}`, or with `functionTry` a function's whole body that is one `try` statement, [dcl.fct.def.general].
    StmtPtr block(bool functionTry = false);
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
    bool conditionDeclaresObject();
    StmtPtr declaringLoop(std::size_t pos, bool forStep);
    void resolveGotos();

    bool staticAssertion();
    // `V<N>` naming the class whose body is being read: steps to the `(` after it when it does.
    bool atOwnTemplateId(const std::string &tag, const std::string &local);
    bool ownArgsThenParen() const;
    const Type *declaringClassOf(const Type *cls, const std::string &name) const;
    void instantiateViableMemberTemplate(const TemplateDecl &mt,
                                         const std::vector<const Type *> &argTypes, std::size_t pos);
    // [temp.deduct.conv] (R6): each conversion function template of `cls` or a base that deduces
    // against `to`, made as the conversion function it then names - a candidate as any other.
    void instantiateConversionTemplates(const Type *cls, const Type *to, std::size_t pos);
    // A class's conversion function templates are kept under this one member name.
    static const char *conversionTemplateKey() { return "operator <template>"; }
    bool deduceFromArgument(const Type *param, const Type *arg, std::vector<const Type *> *binding,
                            std::vector<long long> *values, std::string *why, Packs *packs = nullptr);
    // `using X = T;` is an alias declaration and not a using-declaration: true if one was read.
    bool aliasDeclaration(const std::string &prefix);
    // At `(` after a type: whether it opens a parenthesised declarator - `(*)`, `(&)`, `(S::*)` -
    // rather than the parameter list of a function type, `int(int)`.
    bool atParenDeclarator() const {
        std::size_t q = 1;
        while (peekAt(q).kind == TokenKind::Ident && peekAt(q + 1).is("::")) q += 2;
        return peekAt(1).is("*") || peekAt(1).is("&") || peekAt(1).is("&&") || (q > 1 && peekAt(q).is("*"));
    }
    // ---- WS-C2 (review 2026-10-08): attributes read and ignored, [dcl.attr.grammar]/5 ----
    bool skipAttributes();
    void skipTypeAttributes(const char *what);
    void skipBalanced();
    bool attributedDeclarationAhead();
    // Where the declaration being read wrote `[[noreturn]]`, 0 if it did not - [dcl.attr.noreturn]/1
    // gives it to a function alone.
    std::size_t noreturnAt_ = 0;
    void refuseNoreturnObject(const std::string &name);
    std::size_t lastAttributeEnd_ = static_cast<std::size_t>(-1);
    // `extern template ...;` is read and suppresses nothing: true if one was read.
    bool externTemplateDeclaration();
    bool exceptionSpecification();
    const Type *adjustedSpecType(const Type *t, std::size_t pos);
    // ---- WS-C1 (review 2026-10-08): the virt-specifier-seq and `= default` / `= delete`, read behind the exception specification ----
    VirtSpecifiers pendingVirt_;
    void readVirtSpecifiers();
    VirtSpecifiers takeVirtSpecifiers() { VirtSpecifiers v = pendingVirt_; pendingVirt_ = VirtSpecifiers(); return v; }
    void refuseVirtSpecifiers(const char *what);
    // `= default` or `= delete` after the declarator - [dcl.fct.def.default], [dcl.fct.def.delete] - for the declare* that follows.
    enum class SpecialDef { None, Defaulted, Deleted };
    SpecialDef pendingSpecial_ = SpecialDef::None;
    std::size_t pendingSpecialAt_ = 0;
    void readDefaultedOrDeleted();
    SpecialDef takeSpecialDef() { SpecialDef s = pendingSpecial_; pendingSpecial_ = SpecialDef::None; return s; }
    // A special member written `= default` is the implicit one, with the access it was written under.
    // `quiet`: written `noexcept = default` - the function is noexcept as declared, whatever the implicit one
    // would be (as cl and clang have it; 1.77).
    struct DefaultedMember {
        bool is; Access access; bool quiet;
        DefaultedMember() : is(false), access(Access::Public), quiet(false) {}
    };
    struct Defaulted { DefaultedMember defaultCtor, copyCtor, moveCtor, copyAssign, moveAssign, dtor; };
    std::map<std::string, Defaulted> defaulted_;
    void defaultedMember(DefaultedMember &m, Access access, std::size_t pos, const std::string &what);
    void applyDefaultedAccess(const std::string &tag, const Type *type);
    void refuseDeleted(const Signature &f, std::size_t pos) const;
    Defaulted defaultedFor(const std::string &tag) const;
    static bool deletedSpecial(const Signature *f) { return f != nullptr && f->deleted; }
    static bool nonPublicDefault(const DefaultedMember &m) { return m.is && m.access != Access::Public; }
    void declareDefaultedMoveAssign(const std::string &tag, const Type *type, std::size_t pos);
    // [class.copy]/20 - the implicit move assignment, through declareDefaultedMoveAssign where it has work.
    void declareImplicitMoveAssign(const std::string &tag, const Type *type, std::size_t pos,
                                   bool userDeclared);
    const Signature *moveAssignOf(const Type *cls) const;
    void refuseConstexprConstructorBody(std::size_t parenAt) const;
    void refuseScopedConversion(const Type *from, const Type *to, std::size_t pos) const;
    void refuseScopedOperand(const Type *a, const Type *b, const char *op, std::size_t pos) const;
    // `E::a`, `n::E::a`, `C::E::a` - an enumerator named through its enumeration, [dcl.enum]/11.
    ExprPtr enumeratorThroughEnum();
    bool atSwitchCondition() const;
    // The classes befriended by each class, by tag - [class.friend]/2.
    std::map<std::string, std::vector<std::string> > friendClasses_;
    void befriendClass(const std::string &owner, bool keyed);
    bool isFriendClassOf(const Type *cls) const;
    // ---- end WS-C1 ----
    // Set by exceptionSpecification() at each place a parameter list can be closed, read and cleared by whichever declare* call follows.
    bool pendingNoexcept_ = false;
    // The other half of what exceptionSpecification() read: `throw(T...)` with a type in it.
    DynamicSpec pendingSpec_;
    // **Is the function being parsed declared `noexcept`?** [except.spec]/9.
    bool inNoexceptFunction_ = false;
    // Set when `explicit operator T()` is seen, consumed by the next declareFunction of a conversion name, which stamps the signature.
    bool pendingExplicitConversion_ = false;
    // How many potentially-throwing things the expression being parsed has
    // reached. `noexcept(e)` reads it; nothing else does.
    int mayThrow_ = 0;
    // Set for the one completeCall that follows: its callee expression is a virtual function's slot, and
    // the function is noexcept.
    bool calleeNoexcept_ = false;
    long long constantExpression(const char *what);
    bool foldPointerConstant(const Expr &e, long long *out, std::size_t pos) const;
    bool fold(const Expr &e, long long *out, std::size_t pos) const;

    void typedefFunctionSuffix(Declared &td);

    bool foldAddress(const Expr &e, std::string *sym, long long *off) const;
    bool addressOfObject(const Expr &e, std::string *sym, long long *off) const;
    long long narrowTo(long long v, const Type *t) const;

    ExprPtr expr();
    ExprPtr assign();
    ExprPtr assignFrom(ExprPtr n);
    ExprPtr takeAssigned(ExprPtr &n);
    ExprPtr builtinOperand(ExprPtr v, std::size_t pos);
    ExprPtr afterAssigned(ExprPtr first, ExprPtr r);
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
    // At a `(`: does a type-id run from here to a `)`? [dcl.ambig.res]/2 - `(S::m)` naming a data member does not.
    bool parenHoldsTypeId();
    // `a .* p` and `p ->* q` - [expr.mptr.oper], which sits between a cast and
    // a multiplication. What it builds is the object's address plus the
    // offset the member pointer holds, read as the member's type.
    ExprPtr memberPointerExpr();
    ExprPtr applyMemberPointer(ExprPtr addr, ExprPtr mp, std::size_t pos,
                               bool constObject, bool volatileObject = false);
    // `&S::f` - the ABI's pair, built into a slot of this frame.
    // `vtableCode` is Itanium's 1 + slot offset for a virtual function, 0 for
    // a plain one; `code` names the function - or cl's vcall thunk - to hold.
    ExprPtr boundMemberPointer(const Type *cls, const Signature &f,
                               std::size_t pos, long long vtableCode = 0,
                               const std::string &code = std::string());
    // The null pointer to a member function, the same pair with a null code word (1.27).
    ExprPtr nullMemberFunctionPointer(const Type *mp);
    // `throw e` as an expression - an arm of `?:`, the operand of `noexcept` - of type void, and the
    // statements a `throw` lowers to, which are all expressions, as one comma chain (1.27).
    ExprPtr throwExpression();
    ExprPtr statementsAsExpression(StmtPtr s, std::size_t pos);
    // How many throw-expressions have been read, which tells a `?:` that an arm is one.
    int throwsSeen_ = 0;
    // Whether the function template signature readTemplateDeclaration last read says `noexcept` (1.27).
    bool patternNoexcept_ = false;
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
    // A function named but not called, as a pointer to it (null for none), bare or qualified.
    ExprPtr functionAsValue(const std::string &key, std::size_t pos);
    // [over.over]: what an overloaded function's name converts to while its operand is read.
    const Type *overloadTarget_ = nullptr;
    // Set while a call argument that is a function's name and nothing else is read: its set waits.
    bool overloadArgument_ = false;
    // Held over an initialiser, an assignment's right side, a cast's operand and a return's value.
    struct OverloadTarget {
        Parser &p; const Type *was; bool wasArgument;
        OverloadTarget(Parser &parser, const Type *t, bool argument = false)
            : p(parser), was(parser.overloadTarget_), wasArgument(parser.overloadArgument_) {
            p.overloadTarget_ = t; p.overloadArgument_ = argument;
        }
        ~OverloadTarget() { p.overloadTarget_ = was; p.overloadArgument_ = wasArgument; }
    };
    // The one function of the set whose type `target` names - of `cls` for a member - or null.
    const Signature *pickByTarget(const std::vector<std::size_t> &set, const Type *cls,
                                  const Type *target) const;
    // A function's address as a value, and a member function's as the ABI's pair.
    ExprPtr functionAddress(const Signature &sig, const std::string &key);
    // `&std::pair<int, int>::first`: the class a qualifier holding a template-id names, and in `*member` how many
    // tokens from here its member's name is; null where the tokens are no such qualifier.
    const Type *templateIdQualifier(std::size_t *member);
    ExprPtr memberFunctionAddress(const Type *cls, const Signature &f, const std::string &key,
                                  std::size_t pos);
    // `f`, `&f`, `n::f` or `&S::f` alone up to the argument's `,` or `)`, the next token first; `shifted`, up to
    // whatever may end the right operand of a `<<` or `>>`.
    bool atBareFunctionArgument(bool braced = false, bool shifted = false) const;
    // [over.over]/1: an overloaded name right of `<<` or `>>` on a class - `os << std::endl` - is the function its
    // member operator's function-pointer parameter wants, where one does.
    ExprPtr shiftedFunction(const Type *left, BinOp op, ExprPtr rhs);
    void resolveOverloadPlaceholder(ExprPtr &value, const Type *to, std::size_t pos);
    // The class scope's member functions of `name` as a value - its static ones - or null where it has none.
    ExprPtr memberFunctionByName(const std::string &name, std::size_t pos);
    // [over.over]/1's parameter target: an overloaded name as an argument waits behind a
    // placeholder, as a braced list does, until the parameter it initialises is known.
    struct OverloadArg {
        const Expr *placeholder; long long serial;
        std::vector<std::size_t> set; std::string key; const Type *cls; std::size_t pos;
        // Function templates of the name: their specializations join the set once a type is wanted.
        std::vector<TemplateDecl> templates;
    };
    // [temp.deduct.funcaddr]: each template's specialization of the function type `target` names, made.
    std::vector<std::size_t> specializationsFor(const std::vector<TemplateDecl> &templates, const Type *target,
                                                std::size_t pos);
    // [over.over]/2: a function template named without a call, its specialization by the type wanted.
    ExprPtr templateAsValue(const std::string &name, const std::string &scope, std::size_t pos);
    std::vector<OverloadArg> overloadArgs_;
    long long overloadSerial_ = 0;
    ExprPtr overloadPlaceholder(const std::vector<std::size_t> &set, const std::string &key,
                                const Type *cls, std::size_t pos);
    const OverloadArg *overloadArgOf(const Expr &e) const;
    // The function of the set `param` takes, as a value - null where none or several are of its type.
    ExprPtr overloadArgumentFor(const Type *param, const OverloadArg &oa);
    // Before `key`'s set is ranked: each candidate's parameter picks, and they must pick one function.
    void pickOverloadArguments(const std::string &key, std::vector<ExprPtr> &args,
                               const Type *object);
    // A placeholder that reached a call no parameter picked it on, refused by name.
    void refuseOverloadArguments(const std::vector<ExprPtr> &args, const std::string &callee) const;
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
    // **The non-member it chose, called alone**: resolved again by name, the set would hold the specializations
    // other uses made too, which this one did not deduce - [temp.over]/1 - and ranking one converts for it.
    std::size_t operatorChosen_ = 0;
    Signature resolveChosenOperator(const std::string &name, std::vector<ExprPtr> &args, std::size_t pos);
    // A key of its own for a set made up for one resolution: a nested one of the same name cannot erase it.
    std::string ownKey(const std::string &prefix) { return prefix + "#" + std::to_string(ownKeys_++); }
    unsigned ownKeys_ = 0;
    // `-v`, `!v`, `*p`, `++v`. Null when the operand is not a class or the class has no
    // such operator, and the built-in path below is then reached unchanged - which keeps
    // `&obj` the ordinary address-of for a class that declares no `operator&`.
    ExprPtr overloadedSpelled(const char *spelling, ExprPtr &lhs, ExprPtr &rhs, std::size_t pos);
    ExprPtr overloadedUnary(const char *spelling, ExprPtr &operand,
                            std::size_t pos);
    const Type *ptrdiffType();
    ExprPtr pointerAdd(ExprPtr p, ExprPtr n);
    ExprPtr pointerSub(ExprPtr l, ExprPtr r, std::size_t pos);

    // ----- WS-E2: initialisation, expressions, casts (review 2026-10-08)

    // `base`'s offset in `derived` by an accessible path, -1 if none; *viaVirtual if it crosses a virtual base.
    int downcastOffset(const Type *derived, const Type *base, bool *viaVirtual) const;
    // `(D *)p` for a `B *p`: the pointer moved back by `off` bytes, a null kept null.
    ExprPtr downcastPointer(ExprPtr p, const Type *to, int off);
    // [over.match.list]'s second phase: the braces' elements as a constructor's arguments,
    // `args` filled from `in`; a copy-list-init refuses an `explicit` one; no element narrows.
    Signature listConstructor(const Type *plain, Init &in, std::vector<ExprPtr> &args,
                              bool copyList, std::size_t pos);
    // `T{...}` as an expression, [expr.type.conv]/2 - the '{' still ahead.
    ExprPtr listTemporary(const Type *to, std::size_t pos);
    // A list-initialised temporary of `to` from an already-read list, as an xvalue.
    ExprPtr listTemporaryFrom(const Type *to, Init &in, bool copyList, std::size_t pos);
    // An aggregate built in a hidden local of this frame from its braced list.
    ExprPtr aggregateTemporary(const Type *plain, Init &in, std::size_t pos);
    // A scalar from its one braced element, [dcl.init.list]/3 - narrowing refused.
    ExprPtr scalarFromList(const Type *to, Init &in, std::size_t pos);
    // `new T[n]{...}` (R28): element i's address from the array's slot; a listed element of a
    // class built there; one of a type without constructors as the value stored there.
    ExprPtr elementAddress(const std::string &temp, int slot, const Type *elem, std::size_t i);
    ExprPtr constructListedElement(const Type *made, Init &clause, ExprPtr at, std::size_t pos);
    ExprPtr listedElementValue(const Type *made, Init &clause);
    // A braced list over `ilType`'s element, its backing array's setup folded in front.
    ExprPtr initializerListExpr(const Type *ilType, Init &in, std::size_t pos);
    // A braced call argument is parsed as an Init behind a placeholder and built once
    // the parameter is known - here, before the overload set is ranked.
    struct BracedArg { const Expr *placeholder; Init init; };
    std::vector<BracedArg> bracedArgs_;
    ExprPtr bracedPlaceholder(Init in);
    ExprPtr scalarFromList(const Type *to, std::size_t pos);
    bool isBracedPlaceholder(const Expr &e) const;
    void buildBracedArguments(const std::string &key, std::vector<ExprPtr> &args,
                              std::size_t pos, const Type *object = nullptr);
    void buildBracedArguments(const std::vector<const Type *> &params,
                              std::vector<ExprPtr> &args, std::size_t pos);
    ExprPtr bracedArgumentFor(const Type *param, Init &in, std::size_t pos);
    // Whether a braced list could initialise a parameter of this type at all ([over.ics.list]),
    // which tells the overloads a braced argument could go to from those it cannot (1.25).
    bool bracedListViable(const Type *param, const Init &in);
    const Init *bracedInitOf(const Expr &e) const;
    Signature resolveOverloadBraced(const std::string &key, std::vector<ExprPtr> &args,
                                    std::size_t pos, const Type *object = nullptr,
                                    bool objectLvalue = true);
    bool onlyImplicitConstructors(const std::string &tag) const;
    int listTemps_ = 0;
    // `std::initializer_list<E>` itself, which a braced list makes rather than calls.
    static bool isInitializerListType(const Type *t);
    // `std::initializer_list<elem>`, instantiated through tokens naming a hidden typedef.
    const Type *initializerListOf(const Type *elem, std::size_t pos);
    int ilElemSeq_ = 0;
    // `auto x = {...}`, the '{' ahead: the list's one element type, read and rewound.
    const Type *deduceBracedAuto(const std::string &name, std::size_t pos);
    // [expr.prim.lambda]/6: a captureless closure's `__invoke` and its conversion to a pointer to it.
    void captureFreeConversion(const std::string &tag, const std::string &local, const Type *returns,
                               const std::vector<const Type *> &params, std::size_t pos,
                               std::vector<PendingBody> &bodies);
    int lambdaConvSeq_ = 0;
    // `dynamic_cast` after its operand is read: a pointer target, and a reference one through it.
    // `reference`: the Microsoft runtime is told so and throws std::bad_cast itself on a failure.
    ExprPtr dynamicCastFrom(ExprPtr v, const Type *to, std::size_t pos, bool reference = false);
    ExprPtr dynamicCastReference(ExprPtr v, const Type *to, std::size_t pos);

    Program *current_ = nullptr;
};
